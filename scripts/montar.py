#!/usr/bin/env python3
"""Monta o app NFS Carbon para o TestFlight, a partir do IPA oficial do SpeedBreaker: Carbon.

Só a parte de arquivos (roda em qualquer sistema com Python 3.9 ou mais novo, sem pacotes extras):
  1. confere o sha256 do IPA e do executável contra o config/carbon.conf;
  2. confere que a assinatura provisória do executável pede exatamente os entitlements do config/carbon.entitlements;
  3. extrai o app para dentro de um .xcarchive, mantendo as permissões dos arquivos (o executável continua executável);
  4. ajusta o Info.plist do app: bundle ID, nome na tela, número da build, só iPhone, criptografia isenta e as frases de
     permissão que a Apple cobra no envio;
  5. escreve o Info.plist do archive, o export.plist e o entitlements.plist que o codesign usa.

A assinatura (codesign) e o envio (xcodebuild -exportArchive) ficam no scripts/reassinar.sh, que roda no macOS.

Uso:
  python3 scripts/montar.py --conf config/carbon.conf --entitlements config/carbon.entitlements \
      --ipa <IPA> --saida <pasta> --build <número> --team <TEAM ID>
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import os
import plistlib
import re
import shutil
import stat
import struct
import sys
import zipfile
from pathlib import Path

# Frases de permissão. O SDL3, embutido no app, cita câmera, microfone e Bluetooth; o jogo não usa nenhum dos três, mas a
# Apple costuma cobrar a frase de quem cita a API. O movimento é dos controles por toque (direção por inclinação): ler o
# acelerômetro não pede permissão, a frase só evita uma cobrança por engano.
FRASES = {
    "NSCameraUsageDescription": "O NFS Carbon não usa a câmera.",
    "NSMicrophoneUsageDescription": "O NFS Carbon não usa o microfone.",
    "NSBluetoothAlwaysUsageDescription": "O Bluetooth serve só para controles de jogo.",
    "NSMotionUsageDescription": "O movimento do iPhone serve só para virar o carro, se você escolher a direção por "
                                "inclinação.",
}


class Erro(Exception):
    """Falha com mensagem para o log do Actions."""


def ler_conf(caminho: Path) -> dict[str, str]:
    conf: dict[str, str] = {}
    for n, linha in enumerate(caminho.read_text(encoding="utf-8").splitlines(), 1):
        linha = linha.strip()
        if not linha or linha.startswith("#"):
            continue
        m = re.fullmatch(r'([A-Z0-9_]+)="([^"]*)"', linha)
        if not m:
            raise Erro(f"{caminho}:{n}: linha fora do formato CHAVE=\"valor\": {linha}")
        conf[m.group(1)] = m.group(2)
    faltam = [c for c in ("IPA_SHA256", "APP_NO_IPA", "EXE_NO_APP", "EXE_SHA256", "BUNDLE_ID", "NOME_NA_TELA",
                          "NOME_DO_ARCHIVE") if not conf.get(c)]
    if faltam:
        raise Erro(f"{caminho}: faltam {', '.join(faltam)}")
    return conf


def sha256(caminho: Path) -> str:
    h = hashlib.sha256()
    with open(caminho, "rb") as f:
        for bloco in iter(lambda: f.read(1 << 20), b""):
            h.update(bloco)
    return h.hexdigest()


def entitlements_do_executavel(dados: bytes) -> dict:
    """Os entitlements (XML) gravados na assinatura do executável Mach-O arm64."""
    magic = struct.unpack_from("<I", dados, 0)[0]
    inicio = 0
    if struct.unpack_from(">I", dados, 0)[0] == 0xCAFEBABE:  # binário "fat": pega a fatia arm64
        n = struct.unpack_from(">I", dados, 4)[0]
        for i in range(n):
            cputype, _sub, offset, _size, _align = struct.unpack_from(">IIIII", dados, 8 + 20 * i)
            if cputype == 0x0100000C:  # CPU_TYPE_ARM64
                inicio = offset
                break
        else:
            raise Erro("o executável não tem a fatia arm64")
        magic = struct.unpack_from("<I", dados, inicio)[0]
    if magic != 0xFEEDFACF:
        raise Erro(f"o executável não é Mach-O de 64 bits (magic {magic:#x})")
    ncmds = struct.unpack_from("<I", dados, inicio + 16)[0]
    p = inicio + 32
    for _ in range(ncmds):
        cmd, tamanho = struct.unpack_from("<II", dados, p)
        if cmd == 0x1D:  # LC_CODE_SIGNATURE
            dataoff, datasize = struct.unpack_from("<II", dados, p + 8)
            sig = dados[inicio + dataoff: inicio + dataoff + datasize]
            sb_magic, _len, count = struct.unpack_from(">III", sig, 0)
            if sb_magic != 0xFADE0CC0:
                raise Erro("assinatura do executável em formato inesperado")
            for j in range(count):
                _tipo, off = struct.unpack_from(">II", sig, 12 + 8 * j)
                b_magic, b_len = struct.unpack_from(">II", sig, off)
                if b_magic == 0xFADE7171:  # entitlements em XML
                    return plistlib.loads(sig[off + 8: off + b_len])
            return {}
        p += tamanho
    raise Erro("o executável não tem assinatura (LC_CODE_SIGNATURE)")


def extrair_app(ipa: Path, app_no_ipa: str, destino: Path) -> int:
    """Extrai Payload/<app> do IPA para `destino`, com as permissões gravadas no zip. Devolve quantos arquivos."""
    prefixo = f"Payload/{app_no_ipa}/"
    arquivos = 0
    with zipfile.ZipFile(ipa) as z:
        entradas = [zi for zi in z.infolist() if zi.filename.startswith(prefixo)]
        if not entradas:
            raise Erro(f"o IPA não tem {prefixo}")
        outros = sorted({zi.filename.split("/")[1] for zi in z.infolist()
                         if zi.filename.startswith("Payload/") and zi.filename.count("/") >= 2} - {app_no_ipa})
        if outros:
            raise Erro(f"o IPA tem outro app em Payload/: {outros}")
        for zi in entradas:
            rel = zi.filename[len(prefixo):]
            if not rel:
                continue
            partes = Path(rel).parts
            if rel.startswith("/") or ".." in partes:
                raise Erro(f"caminho suspeito no IPA: {zi.filename}")
            modo = zi.external_attr >> 16
            if stat.S_ISLNK(modo):
                raise Erro(f"link simbólico no IPA (não esperado): {zi.filename}")
            alvo = destino / rel
            if zi.is_dir():
                alvo.mkdir(parents=True, exist_ok=True)
                continue
            alvo.parent.mkdir(parents=True, exist_ok=True)
            with z.open(zi) as origem, open(alvo, "wb") as saida:
                shutil.copyfileobj(origem, saida)
            permissao = stat.S_IMODE(modo)
            os.chmod(alvo, permissao if permissao else 0o644)
            arquivos += 1
    return arquivos


def ajustar_info_plist(caminho: Path, conf: dict[str, str], build: str) -> dict:
    with open(caminho, "rb") as f:
        dados = f.read()
    formato = plistlib.FMT_BINARY if dados.startswith(b"bplist") else plistlib.FMT_XML
    info = plistlib.loads(dados)
    if info.get("CFBundleExecutable") != conf["EXE_NO_APP"]:
        raise Erro(f"CFBundleExecutable inesperado: {info.get('CFBundleExecutable')!r}")
    info["CFBundleIdentifier"] = conf["BUNDLE_ID"]
    info["CFBundleDisplayName"] = conf["NOME_NA_TELA"]
    info["CFBundleVersion"] = build
    info["UIDeviceFamily"] = [1]  # só iPhone
    info["ITSAppUsesNonExemptEncryption"] = False
    for chave, frase in FRASES.items():
        info[chave] = frase
    with open(caminho, "wb") as f:
        plistlib.dump(info, f, fmt=formato, sort_keys=True)
    return info


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--conf", required=True, type=Path)
    ap.add_argument("--entitlements", required=True, type=Path)
    ap.add_argument("--ipa", required=True, type=Path)
    ap.add_argument("--saida", required=True, type=Path, help="pasta de trabalho (o archive é criado dentro dela)")
    ap.add_argument("--build", required=True, help="número da build (o do Actions)")
    ap.add_argument("--team", required=True, help="Team ID da conta Apple (10 caracteres)")
    a = ap.parse_args(argv)

    try:
        conf = ler_conf(a.conf)
        if not re.fullmatch(r"[1-9][0-9]*", a.build):
            raise Erro(f"número de build inválido: {a.build!r}")
        if not re.fullmatch(r"[A-Z0-9]{10}", a.team):
            raise Erro("APPLE_TEAM_ID inválido: são 10 letras maiúsculas e números (Membership details)")

        print(f"[1] IPA: {a.ipa.name}")
        h = sha256(a.ipa)
        if h != conf["IPA_SHA256"]:
            raise Erro(f"sha256 do IPA não confere: {h} (esperado {conf['IPA_SHA256']})")
        print(f"    sha256 ok: {h}")

        archive = a.saida / f"{conf['NOME_DO_ARCHIVE']}.xcarchive"
        if archive.exists():
            shutil.rmtree(archive)
        app = archive / "Products" / "Applications" / conf["APP_NO_IPA"]
        app.mkdir(parents=True)
        (archive / "dSYMs").mkdir()
        n = extrair_app(a.ipa, conf["APP_NO_IPA"], app)
        print(f"[2] app extraído no archive: {n} arquivos")

        exe = app / conf["EXE_NO_APP"]
        h = sha256(exe)
        if h != conf["EXE_SHA256"]:
            raise Erro(f"sha256 do executável não confere: {h} (esperado {conf['EXE_SHA256']})")
        if not os.access(exe, os.X_OK):
            raise Erro("o executável perdeu a permissão de execução na extração")
        print(f"    executável ok: {h}")

        with open(a.entitlements, "rb") as f:
            esperados = plistlib.load(f)
        pedidos = entitlements_do_executavel(exe.read_bytes())
        if pedidos != esperados:
            raise Erro(f"o IPA oficial pede outros entitlements: {sorted(pedidos)} (esperado {sorted(esperados)}); "
                       "conferir a versão nova antes de seguir")
        print(f"[3] entitlements do IPA = config: {', '.join(sorted(pedidos))}")

        for resto in ("_CodeSignature", "embedded.mobileprovision"):
            alvo = app / resto
            if alvo.is_dir():
                shutil.rmtree(alvo)
            elif alvo.exists():
                alvo.unlink()

        info = ajustar_info_plist(app / "Info.plist", conf, a.build)
        versao = info.get("CFBundleShortVersionString", "")
        if not versao:
            raise Erro("o Info.plist oficial não tem CFBundleShortVersionString")
        print(f"[4] Info.plist: {info['CFBundleIdentifier']} \"{info['CFBundleDisplayName']}\" "
              f"versão {versao} build {a.build}, só iPhone, iOS mínimo {info.get('MinimumOSVersion')}")

        agora = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None, microsecond=0)
        info_archive = {
            "ApplicationProperties": {
                "ApplicationPath": f"Applications/{conf['APP_NO_IPA']}",
                "Architectures": ["arm64"],
                "CFBundleIdentifier": conf["BUNDLE_ID"],
                "CFBundleShortVersionString": versao,
                "CFBundleVersion": a.build,
                "Team": a.team,
            },
            "ArchiveVersion": 2,
            "CreationDate": agora,
            "Name": conf["NOME_DO_ARCHIVE"],
            "SchemeName": conf["NOME_DO_ARCHIVE"],
        }
        with open(archive / "Info.plist", "wb") as f:
            plistlib.dump(info_archive, f)

        exportar = {
            "method": "app-store-connect",
            "destination": "upload",
            "teamID": a.team,
            "signingStyle": "automatic",
            "testFlightInternalTestingOnly": True,
            "manageAppVersionAndBuildNumber": False,
            "uploadSymbols": False,
        }
        with open(a.saida / "export.plist", "wb") as f:
            plistlib.dump(exportar, f)
        with open(a.saida / "entitlements.plist", "wb") as f:
            plistlib.dump(esperados, f)
        print(f"[5] archive pronto: {archive}")
        return 0
    except Erro as e:
        print(f"erro: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
