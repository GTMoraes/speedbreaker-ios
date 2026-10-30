#!/usr/bin/env python3
"""Testa o scripts/montar.py contra o IPA oficial (que não fica neste repositório).

Uso: python3 testes/testar_montar.py <SpeedBreaker-Carbon-...-ios.ipa>

Confere que o app montado é o oficial com só as mudanças previstas no Info.plist, e que o script recusa IPA,
executável, entitlements, build e Team ID errados (cada recusa é um teste que precisa falhar do jeito certo).
"""
from __future__ import annotations

import contextlib
import io
import os
import plistlib
import sys
import tempfile
import zipfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / "scripts"))
import montar  # noqa: E402

CONF = RAIZ / "config" / "carbon.conf"
ENT = RAIZ / "config" / "carbon.entitlements"
TEAM = "ABCDE12345"
MUDAM = {"CFBundleIdentifier", "CFBundleDisplayName", "CFBundleVersion", "UIDeviceFamily",
         "ITSAppUsesNonExemptEncryption", *montar.FRASES}

falhas: list[str] = []
checks = 0


def check(cond: bool, texto: str) -> None:
    global checks
    checks += 1
    if not cond:
        falhas.append(texto)
        print(f"  FALHOU: {texto}")


def rodar(args: list[str]) -> tuple[int, str]:
    saida = io.StringIO()
    with contextlib.redirect_stdout(saida), contextlib.redirect_stderr(saida):
        codigo = montar.main(args)
    return codigo, saida.getvalue()


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    ipa = Path(sys.argv[1]).resolve()
    conf = montar.ler_conf(CONF)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        base = ["--conf", str(CONF), "--entitlements", str(ENT), "--ipa", str(ipa), "--saida", str(tmp),
                "--build", "7", "--team", TEAM]

        print("1. montagem normal")
        codigo, log = rodar(base)
        check(codigo == 0, f"montar.py devolveu {codigo}: {log}")
        if codigo != 0:
            print(f"\n{checks} checagens, {len(falhas)} falha(s): a montagem normal falhou, o resto nem roda")
            return 1
        archive = tmp / f"{conf['NOME_DO_ARCHIVE']}.xcarchive"
        app = archive / "Products" / "Applications" / conf["APP_NO_IPA"]

        print("2. o app é o oficial, arquivo por arquivo")
        prefixo = f"Payload/{conf['APP_NO_IPA']}/"
        with zipfile.ZipFile(ipa) as z:
            oficiais = {zi.filename[len(prefixo):]: zi for zi in z.infolist()
                        if zi.filename.startswith(prefixo) and not zi.is_dir()}
            for rel, zi in oficiais.items():
                alvo = app / rel
                if rel.startswith("_CodeSignature/"):
                    check(not alvo.exists(), f"{rel} devia ter sido removido")
                    continue
                if rel == "Info.plist":
                    continue
                check(alvo.is_file() and alvo.read_bytes() == z.read(zi), f"{rel} difere do oficial")
                modo = (zi.external_attr >> 16) & 0o777
                check((alvo.stat().st_mode & 0o777) == modo, f"{rel}: permissão {oct(alvo.stat().st_mode & 0o777)}"
                      f" (oficial {oct(modo)})")
            original = plistlib.loads(z.read(prefixo + "Info.plist"))
        presentes = {str(p.relative_to(app)) for p in app.rglob("*") if p.is_file()}
        esperados = {r for r in oficiais if not r.startswith("_CodeSignature/")}
        check(presentes == esperados, f"arquivos a mais ou a menos: {sorted(presentes ^ esperados)}")
        check(os.access(app / conf["EXE_NO_APP"], os.X_OK), "o executável não ficou executável")

        print("3. Info.plist: só as mudanças previstas")
        dados = (app / "Info.plist").read_bytes()
        check(dados.startswith(b"bplist"), "o Info.plist devia continuar binário, como o oficial")
        novo = plistlib.loads(dados)
        for chave in sorted(set(original) | set(novo)):
            if chave in MUDAM:
                continue
            check(original.get(chave) == novo.get(chave), f"Info.plist: {chave} mudou sem estar previsto")
        check(novo["CFBundleIdentifier"] == conf["BUNDLE_ID"], "bundle ID")
        check(novo["CFBundleDisplayName"] == conf["NOME_NA_TELA"], "nome na tela")
        check(novo["CFBundleVersion"] == "7", "número da build")
        check(novo["UIDeviceFamily"] == [1], "só iPhone")
        check(novo["ITSAppUsesNonExemptEncryption"] is False, "criptografia isenta")
        for chave, frase in montar.FRASES.items():
            check(novo.get(chave) == frase, f"frase {chave}")
        check(novo["CFBundleShortVersionString"] == original["CFBundleShortVersionString"], "versão mantida")

        print("4. archive, export e entitlements")
        info_archive = plistlib.loads((archive / "Info.plist").read_bytes())
        props = info_archive["ApplicationProperties"]
        check(props["ApplicationPath"] == f"Applications/{conf['APP_NO_IPA']}", "ApplicationPath")
        check(props["CFBundleIdentifier"] == conf["BUNDLE_ID"], "bundle no archive")
        check(props["CFBundleVersion"] == "7" and props["Team"] == TEAM, "build e team no archive")
        check(info_archive["ArchiveVersion"] == 2, "ArchiveVersion")
        exportar = plistlib.loads((tmp / "export.plist").read_bytes())
        check(exportar == {"method": "app-store-connect", "destination": "upload", "teamID": TEAM,
                           "signingStyle": "automatic", "testFlightInternalTestingOnly": True,
                           "manageAppVersionAndBuildNumber": False, "uploadSymbols": False}, f"export.plist: {exportar}")
        ent = plistlib.loads((tmp / "entitlements.plist").read_bytes())
        check(ent == {"com.apple.developer.kernel.extended-virtual-addressing": True,
                      "com.apple.developer.kernel.increased-memory-limit": True}, f"entitlements: {ent}")

        print("5. recusas (cada uma tem de falhar)")
        texto_conf = CONF.read_text(encoding="utf-8")

        def com_conf(chave: str, valor: str) -> Path:
            p = tmp / f"conf-{chave}.conf"
            p.write_text(texto_conf.replace(f'{chave}="{conf[chave]}"', f'{chave}="{valor}"'), encoding="utf-8")
            return p

        casos = [
            ("IPA errado", ["--conf", str(com_conf("IPA_SHA256", "0" * 64))], "sha256 do IPA"),
            ("executável errado", ["--conf", str(com_conf("EXE_SHA256", "0" * 64))], "sha256 do executável"),
            ("Team ID inválido", ["--team", "abc"], "APPLE_TEAM_ID"),
            ("build inválida", ["--build", "0"], "build"),
        ]
        ent_falso = tmp / "ent-falso.plist"
        with open(ent_falso, "wb") as f:
            plistlib.dump({"com.apple.developer.kernel.increased-memory-limit": True}, f)
        casos.append(("entitlements diferentes", ["--entitlements", str(ent_falso)], "outros entitlements"))
        for nome, troca, esperado in casos:
            args = list(base)
            for i in range(0, len(troca), 2):
                args[args.index(troca[i]) + 1] = troca[i + 1]
            codigo, log = rodar(args)
            check(codigo == 1 and esperado in log, f"{nome}: devolveu {codigo}, log: {log.strip()[-200:]}")

    print(f"\n{checks} checagens, {len(falhas)} falha(s)")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
