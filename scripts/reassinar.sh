#!/bin/bash
# Monta o NFS Carbon a partir do IPA oficial do SpeedBreaker: Carbon e envia ao TestFlight interno.
#
# Roda no runner macOS do GitHub Actions (.github/workflows/testflight-carbon.yml). Precisa das variáveis:
#   ASC_KEY_ID, ASC_ISSUER_ID, ASC_KEY_P8, APPLE_TEAM_ID  (os secrets do repositório)
#   BUILD                                                  (o número da execução do Actions)
#
# Passos:
#   1. baixa o IPA oficial da versão do config/carbon.conf e o .sha256 publicado com ele;
#   2. scripts/montar.py: confere os hashes e os entitlements, monta o .xcarchive e ajusta o Info.plist;
#   3. assinatura provisória (ad hoc) com os entitlements de memória, para a exportação saber o que pedir;
#   4. xcodebuild -exportArchive: assina com o certificado de distribuição gerenciado pela Apple e envia.
# Nada do app vai para o repositório, para Releases ou para artefato do Actions.
set -euo pipefail
cd "$(dirname "$0")/.."

CONF=config/carbon.conf
# shellcheck source=config/carbon.conf
source "$CONF"

for v in ASC_KEY_ID ASC_ISSUER_ID ASC_KEY_P8 APPLE_TEAM_ID BUILD; do
    if [ -z "${!v:-}" ]; then
        echo "erro: falta a variável $v" >&2
        exit 1
    fi
done

TRAB="${RUNNER_TEMP:-$PWD/build}/nfsc"
rm -rf "$TRAB"
mkdir -p "$TRAB"

echo "== 1. IPA oficial $SB_TAG"
curl -fsSL --retry 3 --retry-delay 5 -o "$TRAB/$IPA_NOME" "$IPA_URL"
curl -fsSL --retry 3 --retry-delay 5 -o "$TRAB/$IPA_NOME.sha256" "$IPA_URL.sha256"
PUBLICADO=$(awk '{print $1; exit}' "$TRAB/$IPA_NOME.sha256")
if [ "$PUBLICADO" != "$IPA_SHA256" ]; then
    echo "erro: o .sha256 publicado ($PUBLICADO) não é o do config ($IPA_SHA256)." >&2
    echo "      O projeto trocou o arquivo desta versão: conferir antes de seguir." >&2
    exit 1
fi
ls -l "$TRAB/$IPA_NOME"

echo "== 2. Montar o archive"
python3 scripts/montar.py --conf "$CONF" --entitlements config/carbon.entitlements \
    --ipa "$TRAB/$IPA_NOME" --saida "$TRAB" --build "$BUILD" --team "$APPLE_TEAM_ID"
ARCHIVE="$TRAB/$NOME_DO_ARCHIVE.xcarchive"
APP="$ARCHIVE/Products/Applications/$APP_NO_IPA"
plutil -lint "$ARCHIVE/Info.plist" "$APP/Info.plist" "$TRAB/export.plist" "$TRAB/entitlements.plist"

echo "== 3. Assinatura provisória com os entitlements de memória"
codesign --force --sign - --entitlements "$TRAB/entitlements.plist" "$APP"
codesign --verify --verbose=1 "$APP"
codesign -d --entitlements - "$APP" 2>/dev/null || true

echo "== 4. Assinar com a Apple e enviar ao TestFlight (build $BUILD)"
K="$TRAB/AuthKey_$ASC_KEY_ID.p8"
printf '%s\n' "$ASC_KEY_P8" > "$K"
trap 'rm -f "$K"' EXIT
AUTH=(-allowProvisioningUpdates -authenticationKeyPath "$K"
      -authenticationKeyID "$ASC_KEY_ID" -authenticationKeyIssuerID "$ASC_ISSUER_ID")
xcodebuild -exportArchive -archivePath "$ARCHIVE" -exportPath "$TRAB/export" \
    -exportOptionsPlist "$TRAB/export.plist" "${AUTH[@]}" 2>&1 | tee export.log
grep -qE "EXPORT SUCCEEDED|Upload succeeded|Uploaded" export.log

if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "### NFS Carbon: build $BUILD enviada"
        echo "SpeedBreaker: Carbon $SB_TAG, bundle \`$BUNDLE_ID\`."
        echo "Em uns 10 a 20 minutos a Apple termina de processar e ela aparece no app TestFlight do iPhone."
    } >> "$GITHUB_STEP_SUMMARY"
fi
echo "== pronto: build $BUILD enviada ao TestFlight"
