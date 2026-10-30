#!/usr/bin/env python3
"""Testes do scripts/injetar.py contra o executável oficial (o de dentro do IPA, Payload/SpeedBreaker.app/SpeedBreaker).

Uso: python3 testes/testar_injetar.py <executável>
Se o llvm-otool estiver instalado, ele relê o resultado por conta própria (um leitor de Mach-O que não é o nosso).
"""
from __future__ import annotations

import contextlib
import io
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / "scripts"))
import injetar  # noqa: E402
import montar  # noqa: E402

falhas = 0
checagens = 0


def check(cond: bool, msg: str) -> None:
    global falhas, checagens
    checagens += 1
    if not cond:
        falhas += 1
        print(f"  FALHOU: {msg}")


def rodar(*args: str) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        r = injetar.main(list(args))
    return r, out.getvalue(), err.getvalue()


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    exe_oficial = Path(sys.argv[1])
    ponte = RAIZ / "Toque" / "ponte_sdl.c"
    dylib = injetar.DYLIB_PADRAO
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        exe = tmp / "SpeedBreaker"
        shutil.copyfile(exe_oficial, exe)
        exe.chmod(0o755)
        original = exe.read_bytes()
        m0 = injetar.MachO(original)

        print("1. a lista de funções da ponte")
        funcoes = injetar.funcoes_da_ponte(ponte)
        check(len(funcoes) == 13, f"13 funções (achou {len(funcoes)})")
        check(funcoes[0] == "SDL_WasInit" and "SDL_GetGamepads" in funcoes and "_ZN2ui8HasModalEv" in funcoes,
              f"nomes: {funcoes}")
        exportados = m0.exportados()
        check(len(exportados) > 1000, f"a árvore de exportação lida inteira ({len(exportados)} símbolos)")
        check(all("_" + f in exportados for f in funcoes), "o executável oficial exporta todas")

        print("2. pôr a camada (no próprio arquivo)")
        r, out, err = rodar("--executavel", str(exe), "--ponte", str(ponte))
        check(r == 0, f"saída 0 ({err.strip()})")
        novo = exe.read_bytes()
        m1 = injetar.MachO(novo)
        check(exe.stat().st_mode & 0o111 == 0o111, "continua executável")
        ass = m0.comandos(injetar.LC_CODE_SIGNATURE)
        check(len(ass) == 1, "o oficial tinha uma assinatura")
        dataoff, datasize = struct.unpack_from("<II", original, ass[0][0] + 8)
        check(not m1.comandos(injetar.LC_CODE_SIGNATURE), "a assinatura saiu")
        check(len(novo) == dataoff, f"arquivo cortado onde a assinatura começava ({len(novo)} vs {dataoff})")
        check(m1.ncmds == m0.ncmds and m1.cmds[-1][1] == injetar.LC_LOAD_WEAK_DYLIB,
              "mesmo número de comandos (um saiu, um entrou), o novo por último")
        check(m1.dylibs() == m0.dylibs() + [dylib], "bibliotecas: as de antes, na mesma ordem, e a camada por último")
        p, _c, t = m1.cmds[-1]
        c, tam, off, ts, cur, compat = struct.unpack_from("<6I", novo, p)
        check((c, tam, off, ts, cur, compat) == (0x80000018, t, 24, 2, 0x10000, 0x10000) and t % 8 == 0,
              f"LC_LOAD_WEAK_DYLIB: {(hex(c), tam, off, ts, hex(cur), hex(compat))}")
        check(novo[p + 24: p + 24 + len(dylib)].decode() == dylib and not any(novo[p + 24 + len(dylib): p + t]),
              "caminho e zeros até o fim do comando")
        le0 = [s for s in m0.segmentos() if s["nome"] == "__LINKEDIT"][0]
        le1 = [s for s in m1.segmentos() if s["nome"] == "__LINKEDIT"][0]
        check(le1["filesize"] == dataoff - le0["fileoff"] and le1["fileoff"] == le0["fileoff"],
              "__LINKEDIT termina onde a assinatura começava")
        check(le1["vmsize"] % 0x4000 == 0 and le1["filesize"] <= le1["vmsize"] < le1["filesize"] + 0x4000,
              "__LINKEDIT: vmsize na página de 16 KB seguinte")
        inicio = m0.inicio_dos_dados()
        check(inicio == 16384, f"o código começa em 16384 ({inicio})")
        check(novo[inicio:] == original[inicio:dataoff], "tudo depois do cabeçalho igual ao oficial")
        check(not any(novo[32 + m1.sizeofcmds: inicio]), "o resto do cabeçalho continua zerado")
        outros0 = [(cc, original[pp: pp + tt]) for pp, cc, tt in m0.cmds if cc != injetar.LC_CODE_SIGNATURE]
        outros1 = [(cc, novo[pp: pp + tt]) for pp, cc, tt in m1.cmds[:-1]]
        iguais = sum(1 for a, b in zip(outros0, outros1) if a == b)
        check(len(outros0) == len(outros1) and iguais == len(outros0) - 1,
              f"só o comando do __LINKEDIT mudou ({iguais} de {len(outros0)} iguais)")
        check(m1.exportados() == exportados, "os símbolos exportados continuam os mesmos")
        check("exporta as 13 funções" in out and "biblioteca 23" in out, f"mensagens: {out}")
        try:
            montar.entitlements_do_executavel(novo)
            check(False, "sem assinatura, montar.py não acha entitlements")
        except montar.Erro:
            check(True, "")

        print("3. recusas (o arquivo não pode mudar)")
        r, _o, err = rodar("--executavel", str(exe), "--ponte", str(ponte))
        check(r == 1 and "já carrega" in err and exe.read_bytes() == novo, f"segunda vez: recusa ({err.strip()})")
        ponte_falsa = tmp / "ponte_sdl.c"
        ponte_falsa.write_text(ponte.read_text(encoding="utf-8").replace(
            '{ "SDL_free", (void **)&p_free },', '{ "SDL_free", (void **)&p_free },\n    { "SDL_NaoExiste", (void **)&p_x },'),
            encoding="utf-8")
        copia = tmp / "copia"
        shutil.copyfile(exe_oficial, copia)
        r, _o, err = rodar("--executavel", str(copia), "--ponte", str(ponte_falsa))
        check(r == 1 and "SDL_NaoExiste" in err and copia.read_bytes() == original,
              f"função que o executável não exporta: recusa ({err.strip()})")
        r, _o, err = rodar("--executavel", str(copia), "--ponte", str(ponte), "--dylib", "/tmp/x.dylib")
        check(r == 1 and copia.read_bytes() == original, "caminho fora do app: recusa")
        lixo = tmp / "lixo"
        lixo.write_bytes(b"\0" * 64)
        r, _o, err = rodar("--executavel", str(lixo), "--ponte", str(ponte))
        check(r == 1 and "Mach-O" in err, f"arquivo que não é Mach-O: recusa ({err.strip()})")
        fat = tmp / "fat"
        fat.write_bytes(struct.pack(">II", 0xCAFEBABE, 1) + bytes(56))
        r, _o, err = rodar("--executavel", str(fat), "--ponte", str(ponte))
        check(r == 1 and "fat" in err, "executável fat: recusa")
        # cabeçalho sem espaço: a primeira seção do __TEXT logo depois dos comandos
        apertado = bytearray(original)
        texto = [s for s in m0.segmentos() if s["nome"] == "__TEXT"][0]
        p_sec = texto["p"] + 72
        struct.pack_into("<I", apertado, p_sec + 48, 32 + m0.sizeofcmds + 16)
        aperto = tmp / "apertado"
        aperto.write_bytes(bytes(apertado))
        r, _o, err = rodar("--executavel", str(aperto), "--ponte", str(ponte))
        check(r == 1 and "sem espaço" in err and aperto.read_bytes() == bytes(apertado),
              f"sem espaço no cabeçalho: recusa ({err.strip()})")

        print("4. --saida não mexe no original")
        r, _o, _e = rodar("--executavel", str(copia), "--ponte", str(ponte), "--saida", str(tmp / "saida"))
        check(r == 0 and copia.read_bytes() == original and (tmp / "saida").read_bytes() == novo,
              "o original fica igual; a saída é igual à do passo 2")

        print("5. outro leitor de Mach-O (llvm-otool)")
        otool = shutil.which("llvm-otool") or shutil.which("llvm-otool-18") or shutil.which("otool")
        if not otool:
            print("  (sem llvm-otool: pulado)")
        else:
            libs = subprocess.run([otool, "-L", str(exe)], capture_output=True, text=True)
            linhas = [x.strip() for x in libs.stdout.splitlines()[1:] if x.strip()]
            check(libs.returncode == 0 and linhas[-1].startswith(dylib) and "weak" in linhas[-1],
                  f"otool -L: a camada por último, fraca ({linhas[-1] if linhas else libs.stderr})")
            check(len(linhas) == len(m0.dylibs()) + 1, f"otool -L: {len(linhas)} bibliotecas")
            cmds = subprocess.run([otool, "-l", str(exe)], capture_output=True, text=True)
            check(cmds.returncode == 0 and "LC_CODE_SIGNATURE" not in cmds.stdout, "otool -l: sem assinatura")
            check(f"filesize {le1['filesize']}" in cmds.stdout, "otool -l: __LINKEDIT com o tamanho novo")

    print(f"\n{checagens} checagens, {falhas} falha(s)")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
