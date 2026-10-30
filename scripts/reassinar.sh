#!/bin/bash
# Monta o NFS Carbon a partir do IPA oficial do SpeedBreaker: Carbon e envia ao TestFlight interno.
#
# Roda no runner macOS do GitHub Actions (.github/workflows/testflight-carbon.yml). Precisa das variáveis:
#   ASC_KEY_ID, ASC_ISSUER_ID, ASC_KEY_P8, APPLE_TEAM_ID  (os secrets do repositório)
#   BUILD                                                  (o número da execução do Actions)
#   TOQUE                                                  (sim: com os controles por toque; não: o app como o oficial)
#
# Passos:
#   1. baixa o IPA oficial da versão do config/carbon.conf e o .sha256 publicado com ele;
#   2. scripts/montar.py: confere os hashes e os entitlements, monta o .xcarchive e ajusta o Info.plist;
#   3. com TOQUE=sim: compila o Toque.framework (pasta Toque/), põe dentro do app e faz o executável carregá-lo
#      (scripts/injetar.py);
#   4. assinatura provisória (ad hoc) com os entitlements de memória, para a exportação saber o que pedir;
#   5. xcodebuild -exportArchive: assina com o certificado de distribuição gerenciado pela Apple e envia.
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
TOQUE="${TOQUE:-sim}"
case "$TOQUE" in
    sim) ;;
    não | nao) TOQUE="não" ;;
    *)
        echo "erro: TOQUE tem de ser sim ou não (veio \"$TOQUE\")" >&2
        exit 1
        ;;
esac

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

if [ "$TOQUE" = sim ]; then
    echo "== 3. Controles por toque: compilar o Toque.framework e pôr no app"
    FW="$APP/Frameworks/Toque.framework"
    mkdir -p "$FW"
    XCODE_VER=$(xcodebuild -version | awk '/^Xcode/ {print $2}')
    XCODE_BUILD=$(xcodebuild -version | awk '/^Build version/ {print $3}')
    SDK_VER=$(xcrun --sdk iphoneos --show-sdk-version)
    SDK_BUILD=$(xcrun --sdk iphoneos --show-sdk-build-version)
    echo "Xcode $XCODE_VER ($XCODE_BUILD), SDK iphoneos $SDK_VER ($SDK_BUILD)"
    xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=17.0 -std=gnu11 -fobjc-arc -O2 \
        -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -DTQ_BUILD="\"$BUILD\"" \
        -I Toque -I Toque/SDL/include \
        -dynamiclib -install_name @rpath/Toque.framework/Toque -current_version 1.0 -compatibility_version 1.0 \
        -framework Foundation -framework UIKit -framework CoreGraphics -framework QuartzCore -framework CoreMotion \
        -framework CoreHaptics \
        Toque/nucleo.c Toque/ponte_sdl.c Toque/Toque.m -o "$FW/Toque" 2>&1 | tee toque.log
    # O que vem do app (o SDL e a interface do SpeedBreaker) é achado por nome quando o app abre: nada disso pode
    # ser importado na ligação, senão o framework dependeria de um SDL que não existe como biblioteca.
    IMPORTADOS=$(xcrun nm -u "$FW/Toque")
    if grep -E '^_SDL_|^__ZN2ui' <<< "$IMPORTADOS"; then
        echo "erro: o framework importa funções do app na ligação (tem de ser por dlsym)" >&2
        exit 1
    fi
    xcrun vtool -show-build "$FW/Toque" | grep -E 'platform|minos|sdk' || true
    cp Toque/Info.plist "$FW/Info.plist"
    IFS=. read -r XMA XMI XPA <<< "$XCODE_VER"
    DTXCODE=$(printf '%d%d%d' "$XMA" "${XMI:-0}" "${XPA:-0}")
    for par in "CFBundleIdentifier=$BUNDLE_ID.toque" "CFBundleVersion=$BUILD" \
        "BuildMachineOSBuild=$(sw_vers -buildVersion)" "DTCompiler=com.apple.compilers.llvm.clang.1_0" \
        "DTPlatformBuild=$SDK_BUILD" "DTPlatformName=iphoneos" "DTPlatformVersion=$SDK_VER" \
        "DTSDKBuild=$SDK_BUILD" "DTSDKName=iphoneos$SDK_VER" "DTXcode=$DTXCODE" "DTXcodeBuild=$XCODE_BUILD"; do
        plutil -replace "${par%%=*}" -string "${par#*=}" "$FW/Info.plist"
    done
    cp Toque/PrivacyInfo.xcprivacy "$FW/PrivacyInfo.xcprivacy"
    plutil -lint "$FW/Info.plist" "$FW/PrivacyInfo.xcprivacy"
    python3 scripts/injetar.py --executavel "$APP/$EXE_NO_APP" --ponte Toque/ponte_sdl.c
    otool -L "$APP/$EXE_NO_APP" | tail -n 3
    codesign --force --sign - --timestamp=none "$FW"
    COM_TOQUE="com os controles por toque"
else
    echo "== 3. Sem os controles por toque: o app como o oficial"
    COM_TOQUE="sem os controles por toque"
fi

echo "== 4. Assinatura provisória com os entitlements de memória"
codesign --force --sign - --timestamp=none --entitlements "$TRAB/entitlements.plist" "$APP"
codesign --verify --deep --verbose=1 "$APP"
codesign -d --entitlements - "$APP" 2>/dev/null || true

echo "== 5. Assinar com a Apple e enviar ao TestFlight (build $BUILD, $COM_TOQUE)"
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
        echo "### NFS Carbon: build $BUILD enviada, $COM_TOQUE"
        echo "SpeedBreaker: Carbon $SB_TAG, bundle \`$BUNDLE_ID\`."
        echo "Em uns 10 a 20 minutos a Apple termina de processar e ela aparece no app TestFlight do iPhone."
    } >> "$GITHUB_STEP_SUMMARY"
fi
echo "== pronto: build $BUILD enviada ao TestFlight, $COM_TOQUE"
