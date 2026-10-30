#!/usr/bin/env python3
"""Põe a camada de toque no executável do app: um LC_LOAD_WEAK_DYLIB que carrega Frameworks/Toque.framework.

Só a parte de arquivos (roda em qualquer sistema com Python 3.9 ou mais novo, sem pacotes extras):
  1. confere que o executável (Mach-O arm64, não "fat") exporta todas as funções que a camada procura, lidas da lista
     FUNCOES do Toque/ponte_sdl.c;
  2. tira a assinatura provisória (LC_CODE_SIGNATURE e o fim do __LINKEDIT), como o `codesign --remove-signature`;
  3. acrescenta o LC_LOAD_WEAK_DYLIB depois de todos os comandos (as bibliotecas que já estavam mantêm os números);
  4. relê o resultado e confere tudo.
Fraca ("weak"): se o framework faltar, o app abre do mesmo jeito, só sem os controles por toque.
A assinatura nova (codesign) fica no scripts/reassinar.sh, que roda no macOS.

Uso:
  python3 scripts/injetar.py --executavel <app>/SpeedBreaker --ponte Toque/ponte_sdl.c \
      [--dylib @executable_path/Frameworks/Toque.framework/Toque] [--saida <arquivo>]
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGIC = 0xCAFEBABE
FAT_MAGIC_64 = 0xCAFEBABF
CPU_TYPE_ARM64 = 0x0100000C
LC_REQ_DYLD = 0x80000000
LC_SEGMENT_64 = 0x19
LC_CODE_SIGNATURE = 0x1D
LC_LOAD_DYLIB = 0x0C
LC_LOAD_WEAK_DYLIB = 0x18 | LC_REQ_DYLD
LC_REEXPORT_DYLIB = 0x1F | LC_REQ_DYLD
LC_LAZY_LOAD_DYLIB = 0x20
LC_LOAD_UPWARD_DYLIB = 0x23 | LC_REQ_DYLD
LC_DYLD_INFO = 0x22
LC_DYLD_INFO_ONLY = 0x22 | LC_REQ_DYLD
LC_DYLD_EXPORTS_TRIE = 0x33 | LC_REQ_DYLD
LC_ENCRYPTION_INFO_64 = 0x2C
CARREGA_DYLIB = (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB, LC_LAZY_LOAD_DYLIB, LC_LOAD_UPWARD_DYLIB)
PAGINA = 0x4000  # arm64 no iOS: páginas de 16 KB
VERSAO_1_0 = 0x10000  # 1.0.0, a mesma do -current_version/-compatibility_version do framework

DYLIB_PADRAO = "@executable_path/Frameworks/Toque.framework/Toque"


class Erro(Exception):
    """Falha com mensagem para o log do Actions."""


def funcoes_da_ponte(ponte: Path) -> list[str]:
    """Os nomes da lista FUNCOES do ponte_sdl.c, na ordem (sem o "_" que o Mach-O põe na frente)."""
    texto = ponte.read_text(encoding="utf-8")
    m = re.search(r"\bFUNCOES\[\]\s*=\s*\{(.*?)\n\};", texto, re.S)
    if not m:
        raise Erro(f"{ponte}: não achei a lista FUNCOES")
    nomes = re.findall(r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*\(void \*\*\)&p_', m.group(1))
    if not nomes or len(nomes) != len(set(nomes)):
        raise Erro(f"{ponte}: lista FUNCOES vazia ou repetida: {nomes}")
    return nomes


class MachO:
    """Um Mach-O arm64 de 64 bits ("thin"), o que o Xcode gera para iPhone."""

    def __init__(self, dados: bytes):
        if len(dados) < 32:
            raise Erro("arquivo pequeno demais para ser um executável")
        if struct.unpack_from(">I", dados, 0)[0] in (FAT_MAGIC, FAT_MAGIC_64):
            raise Erro("executável \"fat\" (mais de uma arquitetura): não esperado no IPA oficial")
        magic, cputype, _sub, filetype, ncmds, sizeofcmds, _flags, _res = struct.unpack_from("<8I", dados, 0)
        if magic != MH_MAGIC_64:
            raise Erro(f"não é um Mach-O de 64 bits (magic {magic:#x})")
        if cputype != CPU_TYPE_ARM64:
            raise Erro(f"não é arm64 (cputype {cputype:#x})")
        if filetype != 2:  # MH_EXECUTE
            raise Erro(f"não é um executável (filetype {filetype})")
        self.dados = dados
        self.ncmds = ncmds
        self.sizeofcmds = sizeofcmds
        self.cmds: list[tuple[int, int, int]] = []  # (offset no arquivo, cmd, cmdsize)
        p = 32
        for _ in range(ncmds):
            if p + 8 > 32 + sizeofcmds:
                raise Erro("comandos de carga cortados")
            cmd, tamanho = struct.unpack_from("<II", dados, p)
            if tamanho < 8 or tamanho % 8 or p + tamanho > 32 + sizeofcmds:
                raise Erro(f"comando de carga com tamanho inválido ({cmd:#x}, {tamanho})")
            self.cmds.append((p, cmd, tamanho))
            p += tamanho
        if p != 32 + sizeofcmds:
            raise Erro("sizeofcmds não bate com a soma dos comandos")

    def comandos(self, tipo: int) -> list[tuple[int, int]]:
        return [(p, t) for p, c, t in self.cmds if c == tipo]

    def segmentos(self) -> list[dict]:
        segs = []
        for p, _t in self.comandos(LC_SEGMENT_64):
            nome = self.dados[p + 8: p + 24].rstrip(b"\0").decode()
            vmaddr, vmsize, fileoff, filesize, _maxp, _initp, nsects, _fl = struct.unpack_from("<4Q4I", self.dados, p + 24)
            secoes = []
            for i in range(nsects):
                s = p + 72 + 80 * i
                snome = self.dados[s: s + 16].rstrip(b"\0").decode()
                _addr, tam, off = struct.unpack_from("<QQI", self.dados, s + 32)
                tipo = struct.unpack_from("<I", self.dados, s + 64)[0] & 0xFF
                secoes.append({"nome": snome, "offset": off, "tamanho": tam, "tipo": tipo})
            segs.append({"nome": nome, "p": p, "vmaddr": vmaddr, "vmsize": vmsize, "fileoff": fileoff,
                         "filesize": filesize, "secoes": secoes})
        return segs

    def dylibs(self) -> list[str]:
        nomes = []
        for p, c, _t in self.cmds:
            if c in CARREGA_DYLIB:
                off = struct.unpack_from("<I", self.dados, p + 8)[0]
                fim = self.dados.index(b"\0", p + off)
                nomes.append(self.dados[p + off: fim].decode())
        return nomes

    def exportados(self) -> set[str]:
        """Os símbolos da árvore de exportação (LC_DYLD_EXPORTS_TRIE ou LC_DYLD_INFO_ONLY)."""
        arvore = None
        for p, c, _t in self.cmds:
            if c == LC_DYLD_EXPORTS_TRIE:
                arvore = struct.unpack_from("<II", self.dados, p + 8)
            elif c in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
                v = struct.unpack_from("<10I", self.dados, p + 8)
                arvore = (v[8], v[9])
        if not arvore or not arvore[1]:
            return set()
        b = self.dados[arvore[0]: arvore[0] + arvore[1]]

        def uleb(i: int) -> tuple[int, int]:
            r = s = 0
            while True:
                x = b[i]
                i += 1
                r |= (x & 0x7F) << s
                s += 7
                if x < 0x80:
                    return r, i

        nomes: set[str] = set()
        pilha = [(0, b"")]
        vistos = set()
        while pilha:
            off, prefixo = pilha.pop()
            if off in vistos or off >= len(b):
                raise Erro("árvore de exportação inválida")
            vistos.add(off)
            info, i = uleb(off)
            if info:
                nomes.add(prefixo.decode(errors="replace"))
            i += info
            filhos = b[i]
            i += 1
            for _ in range(filhos):
                fim = b.index(b"\0", i)
                aresta = b[i:fim]
                filho, i = uleb(fim + 1)
                pilha.append((filho, prefixo + aresta))
        return nomes

    def inicio_dos_dados(self) -> int:
        """Onde começa o primeiro byte que não é cabeçalho (o espaço livre para comandos acaba aí)."""
        limite = len(self.dados)
        for seg in self.segmentos():
            for s in seg["secoes"]:
                if s["offset"] and s["tipo"] not in (0x01, 0x0C, 0x12):  # zerofill, gb_zerofill, thread_local_zerofill
                    limite = min(limite, s["offset"])
            if seg["fileoff"] and seg["filesize"]:
                limite = min(limite, seg["fileoff"])
        for p, _t in self.comandos(LC_ENCRYPTION_INFO_64):
            cryptoff = struct.unpack_from("<I", self.dados, p + 8)[0]
            if cryptoff:
                limite = min(limite, cryptoff)
        return limite


def sem_assinatura(m: MachO) -> bytes:
    """O executável sem a assinatura: sem o LC_CODE_SIGNATURE, com o __LINKEDIT encurtado e o arquivo cortado."""
    assinaturas = m.comandos(LC_CODE_SIGNATURE)
    if not assinaturas:
        return m.dados
    if len(assinaturas) > 1:
        raise Erro("mais de um LC_CODE_SIGNATURE")
    p, tamanho = assinaturas[0]
    dataoff, datasize = struct.unpack_from("<II", m.dados, p + 8)
    linkedit = [s for s in m.segmentos() if s["nome"] == "__LINKEDIT"]
    if len(linkedit) != 1:
        raise Erro("sem __LINKEDIT (ou mais de um)")
    le = linkedit[0]
    if m.segmentos()[-1]["nome"] != "__LINKEDIT":
        raise Erro("o __LINKEDIT não é o último segmento")
    fim_le = le["fileoff"] + le["filesize"]
    if not (le["fileoff"] <= dataoff and dataoff + datasize <= fim_le == len(m.dados)):
        raise Erro("a assinatura não está no fim do __LINKEDIT, no fim do arquivo")
    if any(m.dados[dataoff + datasize:]):
        raise Erro("dados depois da assinatura")
    novo = bytearray(m.dados[:dataoff])
    # tira o comando: os seguintes sobem, o fim da área de comandos vira zero
    fim_cmds = 32 + m.sizeofcmds
    novo[p: fim_cmds - tamanho] = m.dados[p + tamanho: fim_cmds]
    novo[fim_cmds - tamanho: fim_cmds] = bytes(tamanho)
    struct.pack_into("<II", novo, 16, m.ncmds - 1, m.sizeofcmds - tamanho)
    # o __LINKEDIT (que vem antes do comando tirado, ou depois e subiu junto)
    p_le = le["p"] if le["p"] < p else le["p"] - tamanho
    filesize = dataoff - le["fileoff"]
    vmsize = (filesize + PAGINA - 1) // PAGINA * PAGINA
    struct.pack_into("<QQQQ", novo, p_le + 24, le["vmaddr"], vmsize, le["fileoff"], filesize)
    return bytes(novo)


def com_dylib(m: MachO, caminho: str) -> bytes:
    """Acrescenta um LC_LOAD_WEAK_DYLIB depois de todos os comandos."""
    if caminho in m.dylibs():
        raise Erro(f"o executável já carrega {caminho} (já tem a camada?)")
    nome = caminho.encode() + b"\0"
    tamanho = (24 + len(nome) + 7) // 8 * 8
    fim_cmds = 32 + m.sizeofcmds
    livre = m.inicio_dos_dados() - fim_cmds
    if tamanho > livre:
        raise Erro(f"sem espaço no cabeçalho para mais um comando ({tamanho} bytes, livres {livre})")
    if any(m.dados[fim_cmds: fim_cmds + tamanho]):
        raise Erro("o espaço depois dos comandos não está zerado")
    cmd = struct.pack("<IIIIII", LC_LOAD_WEAK_DYLIB, tamanho, 24, 2, VERSAO_1_0, VERSAO_1_0) + nome
    cmd += bytes(tamanho - len(cmd))
    novo = bytearray(m.dados)
    novo[fim_cmds: fim_cmds + tamanho] = cmd
    struct.pack_into("<II", novo, 16, m.ncmds + 1, m.sizeofcmds + tamanho)
    return bytes(novo)


def conferir(original: MachO, novo: MachO, caminho: str) -> list[str]:
    """Relê o resultado. Devolve a lista de problemas (vazia: tudo certo)."""
    problemas = []
    ass = original.comandos(LC_CODE_SIGNATURE)
    tam_ass = ass[0][1] if ass else 0
    if novo.comandos(LC_CODE_SIGNATURE):
        problemas.append("a assinatura continua lá")
    if novo.ncmds != original.ncmds - len(ass) + 1:
        problemas.append(f"ncmds {novo.ncmds} (esperado {original.ncmds - len(ass) + 1})")
    ultimo_p, ultimo_c, ultimo_t = novo.cmds[-1]
    if ultimo_c != LC_LOAD_WEAK_DYLIB:
        problemas.append("o último comando não é o LC_LOAD_WEAK_DYLIB")
    else:
        _c, _t, off, ts, cur, compat = struct.unpack_from("<6I", novo.dados, ultimo_p)
        fim = novo.dados.index(b"\0", ultimo_p + off)
        if novo.dados[ultimo_p + off: fim].decode() != caminho or (off, ts, cur, compat) != (24, 2, VERSAO_1_0, VERSAO_1_0):
            problemas.append("o LC_LOAD_WEAK_DYLIB não tem o caminho ou as versões esperadas")
    if novo.dylibs() != original.dylibs() + [caminho]:
        problemas.append("as bibliotecas que já estavam mudaram de ordem")
    if novo.sizeofcmds != original.sizeofcmds - tam_ass + ultimo_t:
        problemas.append("sizeofcmds inesperado")
    # todos os outros comandos iguais, byte a byte, na mesma ordem (menos a assinatura e o __LINKEDIT)
    antes = [(c, original.dados[p: p + t]) for p, c, t in original.cmds if c != LC_CODE_SIGNATURE]
    depois = [(c, novo.dados[p: p + t]) for p, c, t in novo.cmds[:-1]]
    if [c for c, _ in antes] != [c for c, _ in depois]:
        problemas.append("a ordem dos comandos mudou")
    else:
        for (c, a), (_c, d) in zip(antes, depois):
            if a != d and not (c == LC_SEGMENT_64 and a[8:24].rstrip(b"\0") == b"__LINKEDIT"):
                problemas.append(f"o comando {c:#x} mudou")
    segs_a = {s["nome"]: s for s in original.segmentos()}
    segs_d = {s["nome"]: s for s in novo.segmentos()}
    if segs_a.keys() != segs_d.keys():
        problemas.append("os segmentos mudaram")
    else:
        for nome, a in segs_a.items():
            d = segs_d[nome]
            if nome == "__LINKEDIT":
                if ass:
                    dataoff = struct.unpack_from("<I", original.dados, ass[0][0] + 8)[0]
                    if d["filesize"] != dataoff - a["fileoff"] or d["vmsize"] % PAGINA or d["vmsize"] < d["filesize"]:
                        problemas.append("__LINKEDIT com tamanho errado")
                if d["fileoff"] != a["fileoff"] or d["vmaddr"] != a["vmaddr"]:
                    problemas.append("__LINKEDIT mudou de lugar")
            elif a != dict(d, p=a["p"]):
                problemas.append(f"o segmento {nome} mudou")
    le = segs_d.get("__LINKEDIT")
    if le and len(novo.dados) != le["fileoff"] + le["filesize"]:
        problemas.append("o arquivo não termina no fim do __LINKEDIT")
    inicio = original.inicio_dos_dados()
    if novo.dados[inicio:] != original.dados[inicio: len(novo.dados)]:
        problemas.append("o conteúdo depois do cabeçalho mudou")
    if 32 + novo.sizeofcmds > novo.inicio_dos_dados():
        problemas.append("os comandos invadem o código")
    if novo.exportados() != original.exportados():
        problemas.append("os símbolos exportados mudaram")
    return problemas


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--executavel", required=True, type=Path)
    ap.add_argument("--ponte", required=True, type=Path, help="Toque/ponte_sdl.c (a lista de funções que a camada usa)")
    ap.add_argument("--dylib", default=DYLIB_PADRAO)
    ap.add_argument("--saida", type=Path, help="onde gravar (padrão: o próprio executável)")
    a = ap.parse_args(argv)
    try:
        if not a.dylib.startswith("@executable_path/") or "\0" in a.dylib:
            raise Erro(f"caminho do framework inesperado: {a.dylib!r}")
        dados = a.executavel.read_bytes()
        original = MachO(dados)
        print(f"[1] executável: {len(dados):,} bytes, {original.ncmds} comandos, {len(original.dylibs())} bibliotecas")

        funcoes = funcoes_da_ponte(a.ponte)
        exportados = original.exportados()
        faltam = [f for f in funcoes if "_" + f not in exportados]
        if faltam:
            raise Erro(f"o executável não exporta {', '.join(faltam)}: a camada não funcionaria com esta versão")
        print(f"[2] exporta as {len(funcoes)} funções que a camada usa")

        sem = MachO(sem_assinatura(original))
        cortados = len(dados) - len(sem.dados)
        print(f"[3] assinatura provisória tirada ({cortados:,} bytes)" if cortados else "[3] não tinha assinatura")

        novo = MachO(com_dylib(sem, a.dylib))
        problemas = conferir(original, novo, a.dylib)
        if problemas:
            raise Erro("o resultado não passou na conferência: " + "; ".join(problemas))
        livre = novo.inicio_dos_dados() - 32 - novo.sizeofcmds
        print(f"[4] LC_LOAD_WEAK_DYLIB {a.dylib} (biblioteca {len(novo.dylibs())}, {livre:,} bytes livres no cabeçalho)")

        destino = a.saida or a.executavel
        modo = a.executavel.stat().st_mode
        destino.write_bytes(novo.dados)
        destino.chmod(modo)
        print(f"[5] gravado: {destino} ({len(novo.dados):,} bytes)")
        return 0
    except Erro as e:
        print(f"erro: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
