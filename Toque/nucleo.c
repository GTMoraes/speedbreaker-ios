/*
 * Toque: núcleo em C dos controles por toque (ver nucleo.h).
 *
 * As medidas são as do port do NFSU2 (nfsu2-web, web/src/app/style.css e touch.ts), em pontos no tamanho 100%:
 * redondos pequenos de 48 numa fileira no alto (de 56 em 56), ◀ ▶ de 80 x 104, Gás de 84 x 148, Freio de 84 x 104,
 * Nitro redondo de 60, Freio de mão de 56; no Menu, teclas de 44 e pílulas de 100 x 44. As margens são as seguras,
 * com 12 pontos no mínimo. O que só o Carbon tem (Speedbreaker, Equipe, Entrar, os botões de modo, X, Y, LB, RB,
 * Back e Start) entra no mesmo desenho.
 */
#include "nucleo.h"

#include <math.h>
#include <string.h>

#define APERTO_MINIMO 0.15     /* s: um toque rápido segura o botão pelo menos isso (NFSU2: HOLD_MS) */
#define SEGURAR_CAMERA 0.25    /* s: Câmera segurada vira "olhar para trás" (NFSU2: LONG_MS) */
#define RAMPA_IDA 0.18         /* s: botões de direção, do centro ao fim de curso */
#define RAMPA_VOLTA 0.08       /* s: e de volta ao centro */
#define INCLINACAO_FIM 30.0f   /* graus de inclinação para o fim de curso (NFSU2: TILT_DEG) */
#define TRAVA_ANALOGICO 64.0f  /* pontos do centro ao fim de curso no analógico (NFSU2: STICK_PX) */
#define LARGURA_UTIL 608.0f    /* pontos que os controles ocupam no 100% (NFSU2: ROOM) */
#define ALTURA_UTIL 324.0f

static float limitar(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

void tq_ajustes_padrao(tq_ajustes *aj)
{
    memset(aj, 0, sizeof(*aj));
    aj->direcao = TQ_DIRECAO_BOTOES;
    aj->escala = 1.0f;
    aj->cambio_manual = 0;
    aj->pausa_vai_para_menu = 1;
    /* Controles do manual do Carbon de Xbox 360. Mapa e SMS: o direcional ← e → (a tela de controles do jogo de 360;
     * o manual só diz "direcional"). As marchas: a confirmar no jogo. */
    aj->mapa[TQ_ACAO_FREIO_MAO] = TQ_O_A;
    aj->mapa[TQ_ACAO_NITRO] = TQ_O_B;
    aj->mapa[TQ_ACAO_SPEEDBREAKER] = TQ_O_X;
    aj->mapa[TQ_ACAO_PARCEIRO] = TQ_O_Y;
    aj->mapa[TQ_ACAO_RESET] = TQ_O_LB;
    aj->mapa[TQ_ACAO_CAMERA] = TQ_O_RB;
    aj->mapa[TQ_ACAO_OLHAR_TRAS] = TQ_O_RS;
    aj->mapa[TQ_ACAO_PAUSA] = TQ_O_START;
    aj->mapa[TQ_ACAO_ENTRAR] = TQ_O_BACK;
    aj->mapa[TQ_ACAO_MAPA] = TQ_O_DLEFT;
    aj->mapa[TQ_ACAO_SMS] = TQ_O_DRIGHT;
    aj->mapa[TQ_ACAO_SOBE] = TQ_O_RUP;
    aj->mapa[TQ_ACAO_DESCE] = TQ_O_RDOWN;
}

static void corrigir_ajustes(tq_ajustes *aj)
{
    if (aj->direcao < TQ_DIRECAO_BOTOES || aj->direcao > TQ_DIRECAO_INCLINACAO)
        aj->direcao = TQ_DIRECAO_BOTOES;
    if (!(aj->escala >= 0.8f && aj->escala <= 1.5f))
        aj->escala = 1.0f;
    for (int i = 0; i < TQ_ACAO_COUNT; i++)
        if (aj->mapa[i] >= TQ_O_COUNT)
            aj->mapa[i] = TQ_O_A;
}

static void refazer_layout(tq_nucleo *n);

void tq_iniciar(tq_nucleo *n, const tq_ajustes *aj)
{
    memset(n, 0, sizeof(*n));
    n->aj = *aj;
    corrigir_ajustes(&n->aj);
    n->modo = TQ_MODO_MENU;
    n->tick_inicial = 1;
    n->selecionado = -1;
    n->s = 1;
    for (int i = 0; i < TQ_MAX_DEDOS; i++)
        n->dedos[i].controle = -1;
    for (int t = 0; t < TQ_TELAS; t++)
        for (int c = 0; c < TQ_C_COUNT; c++)
            n->pos[t][c].tamanho = 1;
}

void tq_definir_ajustes(tq_nucleo *n, const tq_ajustes *aj)
{
    n->aj = *aj;
    corrigir_ajustes(&n->aj);
    refazer_layout(n);
    n->eventos |= TQ_EV_LAYOUT;
}

void tq_definir_modo(tq_nucleo *n, int modo)
{
    modo = modo == TQ_MODO_PILOTAGEM ? TQ_MODO_PILOTAGEM : TQ_MODO_MENU;
    if (n->modo == modo)
        return;
    n->modo = modo;
    n->selecionado = -1;
    n->eventos |= TQ_EV_MODO | TQ_EV_LAYOUT;
    refazer_layout(n);
}

int tq_tela_atual(const tq_nucleo *n)
{
    if (n->modo == TQ_MODO_MENU)
        return TQ_TELA_MENU;
    switch (n->aj.direcao) {
    case TQ_DIRECAO_ANALOGICO: return TQ_TELA_ANALOGICO;
    case TQ_DIRECAO_INCLINACAO: return TQ_TELA_INCLINACAO;
    default: return TQ_TELA_BOTOES;
    }
}

/* ---------- posições ---------- */

/* Um controle: canto de cima à esquerda (x, y), largura e altura, raio dos cantos; já em pontos. */
static void por(tq_nucleo *n, int c, tq_forma forma, float x, float y, float w, float h, float canto)
{
    tq_info_controle *k = &n->c[c];
    k->visivel = 1;
    k->forma = forma;
    k->cx = x + w / 2;
    k->cy = y + h / 2;
    k->meia_l = w / 2;
    k->meia_a = h / 2;
    k->canto = canto;
    k->rotulo = "";
    k->icone = TQ_ICONE_NENHUM;
    k->letra = 16 * n->s;
    k->tam_icone = 24 * n->s;
    k->traco = 2;
    k->borda = 0;
}

static void com_texto(tq_nucleo *n, int c, const char *rotulo, float letra)
{
    n->c[c].rotulo = rotulo;
    n->c[c].letra = letra * n->s;
}

static void com_icone(tq_nucleo *n, int c, int icone, float tam, float traco)
{
    n->c[c].icone = icone;
    n->c[c].tam_icone = tam * n->s;
    n->c[c].traco = traco;
}

static void aplicar_posicoes(tq_nucleo *n);

static void refazer_layout(tq_nucleo *n)
{
    for (int i = 0; i < TQ_C_COUNT; i++)
        n->c[i].visivel = 0;
    if (n->w <= 0 || n->h <= 0)
        return;
    const float W = n->w, H = n->h;
    const float l = fmaxf(12, n->se), r = fmaxf(12, n->sd), t = fmaxf(12, n->st), b = fmaxf(12, n->sb);
    /* o tamanho escolhido, até onde os controles cabem entre as margens (NFSU2: autoTouchSize) */
    float s = n->aj.escala;
    const float cabe = fminf((W - l - r) / LARGURA_UTIL, (H - t - b) / ALTURA_UTIL);
    if (s > cabe)
        s = cabe;
    if (s < 0.5f)
        s = 0.5f;
    n->s = s;
    const float P = 48 * s; /* os redondos pequenos do alto */

    por(n, TQ_C_AJUSTES, TQ_FORMA_CIRCULO, l, t, P, P, P / 2);
    com_icone(n, TQ_C_AJUSTES, TQ_ICONE_MENU, 24, 2);

    if (n->modo == TQ_MODO_MENU) {
        /* NFSU2: o direcional em quatro teclas embaixo à esquerda, Voltar sobre OK embaixo à direita; as teclas do
         * tamanho que a faixa ao lado de um menu 4:3 deixa (44 a 52), Voltar e OK de 96 a 136 de largura */
        const float faixa = (W - H * 4 / 3) / 2;
        const float k = limitar((faixa - l - 16 * s) / 3, 44 * s, 52 * s), g = 4 * s, ap = 40 * s;
        const float wl = limitar(faixa - r - 8 * s, 96 * s, 136 * s);
        por(n, TQ_C_DCIMA, TQ_FORMA_RETANGULO, l + k + g, H - b - 2 * (k + g) - k, k, k, 12 * s);
        com_icone(n, TQ_C_DCIMA, TQ_ICONE_CIMA, 24, 2);
        por(n, TQ_C_DESQ, TQ_FORMA_RETANGULO, l, H - b - (k + g) - k, k, k, 12 * s);
        com_icone(n, TQ_C_DESQ, TQ_ICONE_ESQUERDA, 24, 2);
        por(n, TQ_C_DDIR, TQ_FORMA_RETANGULO, l + 2 * (k + g), H - b - (k + g) - k, k, k, 12 * s);
        com_icone(n, TQ_C_DDIR, TQ_ICONE_DIREITA, 24, 2);
        por(n, TQ_C_DBAIXO, TQ_FORMA_RETANGULO, l + k + g, H - b - k, k, k, 12 * s);
        com_icone(n, TQ_C_DBAIXO, TQ_ICONE_BAIXO, 24, 2);
        por(n, TQ_C_VOLTAR, TQ_FORMA_RETANGULO, W - r - wl, H - b - (k + 12 * s) - k, wl, k, k / 2);
        com_texto(n, TQ_C_VOLTAR, "Voltar", 16);
        por(n, TQ_C_OK, TQ_FORMA_RETANGULO, W - r - wl, H - b - k, wl, k, k / 2);
        com_texto(n, TQ_C_OK, "OK", 16);
        /* o que o Carbon tem a mais: X e Y sobre o Voltar, LB e RB nos cantos de cima, Back e Start no meio */
        const float yxy = H - b - (2 * k + 24 * s) - k;
        por(n, TQ_C_X, TQ_FORMA_RETANGULO, W - r - wl, yxy, k, k, 12 * s);
        com_texto(n, TQ_C_X, "X", 16);
        por(n, TQ_C_Y, TQ_FORMA_RETANGULO, W - r - k, yxy, k, k, 12 * s);
        com_texto(n, TQ_C_Y, "Y", 16);
        por(n, TQ_C_LB, TQ_FORMA_RETANGULO, l + 56 * s, t + 4 * s, 60 * s, ap, ap / 2);
        com_texto(n, TQ_C_LB, "LB", 16);
        por(n, TQ_C_RB, TQ_FORMA_RETANGULO, W - r - 60 * s, t + 4 * s, 60 * s, ap, ap / 2);
        com_texto(n, TQ_C_RB, "RB", 16);
        por(n, TQ_C_MODO, TQ_FORMA_RETANGULO, W / 2 - 52 * s, t + 4 * s, 104 * s, ap, ap / 2);
        com_texto(n, TQ_C_MODO, "Pilotar", 16);
        por(n, TQ_C_BACK, TQ_FORMA_RETANGULO, W / 2 - 60 * s - 72 * s, t + 4 * s, 72 * s, ap, ap / 2);
        com_texto(n, TQ_C_BACK, "Back", 16);
        por(n, TQ_C_START, TQ_FORMA_RETANGULO, W / 2 + 60 * s, t + 4 * s, 72 * s, ap, ap / 2);
        com_texto(n, TQ_C_START, "Start", 16);
        for (int c = 0; c < TQ_C_COUNT; c++)
            if (n->c[c].visivel && c != TQ_C_AJUSTES)
                n->c[c].borda = 1; /* NFSU2: um aro fino nos controles do menu */
        aplicar_posicoes(n);
        return;
    }

    const int d = n->aj.direcao;
    /* a fileira do alto, depois do botão de menu (NFSU2: pausa, câmera, reset, mapa, mensagens; e a Equipe no
     * lugar do Drift) */
    por(n, TQ_C_PAUSA, TQ_FORMA_CIRCULO, l + 56 * s, t, P, P, P / 2);
    com_icone(n, TQ_C_PAUSA, TQ_ICONE_PAUSA, 24, 2);
    por(n, TQ_C_CAMERA, TQ_FORMA_CIRCULO, l + 112 * s, t, P, P, P / 2);
    com_icone(n, TQ_C_CAMERA, TQ_ICONE_CAMERA, 24, 2);
    por(n, TQ_C_RESET, TQ_FORMA_CIRCULO, l + 168 * s, t, P, P, P / 2);
    com_icone(n, TQ_C_RESET, TQ_ICONE_RESET, 24, 2);
    por(n, TQ_C_MAPA, TQ_FORMA_CIRCULO, l + 224 * s, t, P, P, P / 2);
    com_icone(n, TQ_C_MAPA, TQ_ICONE_MAPA, 24, 2);
    por(n, TQ_C_SMS, TQ_FORMA_CIRCULO, l + 280 * s, t, P, P, P / 2);
    com_icone(n, TQ_C_SMS, TQ_ICONE_SMS, 24, 2);
    por(n, TQ_C_PARCEIRO, TQ_FORMA_CIRCULO, l + 336 * s, t, P, P, P / 2);
    com_texto(n, TQ_C_PARCEIRO, "Equipe", 12);
    /* no canto direito, o jeito de dirigir (NFSU2: o botão de modo); ao lado, o Reto da inclinação e o Menu */
    por(n, TQ_C_DIRECAO, TQ_FORMA_CIRCULO, W - r - P, t, P, P, P / 2);
    com_icone(n, TQ_C_DIRECAO,
              d == TQ_DIRECAO_ANALOGICO ? TQ_ICONE_ANALOGICO
                                        : (d == TQ_DIRECAO_INCLINACAO ? TQ_ICONE_INCLINACAO : TQ_ICONE_BOTOES),
              24, 2);
    float xm = W - r - P - 8 * s;
    if (d == TQ_DIRECAO_INCLINACAO) {
        por(n, TQ_C_RETO, TQ_FORMA_CIRCULO, xm - P, t, P, P, P / 2);
        com_icone(n, TQ_C_RETO, TQ_ICONE_RETO, 24, 2);
        xm -= P + 8 * s;
    }
    por(n, TQ_C_MODO, TQ_FORMA_RETANGULO, xm - 88 * s, t + 4 * s, 88 * s, 40 * s, 20 * s);
    com_texto(n, TQ_C_MODO, "Menu", 16);

    /* a mão esquerda: a direção, as marchas acima dela e o Entrar (o Start do NFSU2) acima de tudo */
    const float gb = (d == TQ_DIRECAO_ANALOGICO ? 132 : 112) * s;
    if (d == TQ_DIRECAO_BOTOES) {
        por(n, TQ_C_ESQUERDA, TQ_FORMA_RETANGULO, l, H - b - 104 * s, 80 * s, 104 * s, 16 * s);
        com_icone(n, TQ_C_ESQUERDA, TQ_ICONE_ESQUERDA, 32, 2);
        por(n, TQ_C_DIREITA, TQ_FORMA_RETANGULO, l + 88 * s, H - b - 104 * s, 80 * s, 104 * s, 16 * s);
        com_icone(n, TQ_C_DIREITA, TQ_ICONE_DIREITA, 32, 2);
    } else if (d == TQ_DIRECAO_ANALOGICO) {
        /* a base parada no canto (NFSU2: 120 pontos); o polegar pousa em qualquer lugar da área dela */
        por(n, TQ_C_ANALOGICO, TQ_FORMA_CIRCULO, l, H - b - 120 * s, 120 * s, 120 * s, 60 * s);
    }
    if (n->aj.cambio_manual) {
        por(n, TQ_C_DESCE, TQ_FORMA_RETANGULO, l, H - b - gb - 56 * s, 80 * s, 56 * s, 16 * s);
        com_icone(n, TQ_C_DESCE, TQ_ICONE_MARCHA_BAIXO, 32, 2.5f);
        por(n, TQ_C_SOBE, TQ_FORMA_RETANGULO, l + 88 * s, H - b - gb - 56 * s, 80 * s, 56 * s, 16 * s);
        com_icone(n, TQ_C_SOBE, TQ_ICONE_MARCHA_CIMA, 32, 2.5f);
    }
    por(n, TQ_C_ENTRAR, TQ_FORMA_RETANGULO, l, H - b - (n->aj.cambio_manual ? gb + 72 * s : 128 * s) - 40 * s, 92 * s,
        40 * s, 20 * s);
    com_texto(n, TQ_C_ENTRAR, "Entrar", 16);

    /* a mão direita: os pedais (NFSU2) e o Speedbreaker ao lado do Freio */
    por(n, TQ_C_GAS, TQ_FORMA_RETANGULO, W - r - 84 * s, H - b - 148 * s, 84 * s, 148 * s, 16 * s);
    com_texto(n, TQ_C_GAS, "G\xC3\xA1s", 16);
    if (d != TQ_DIRECAO_INCLINACAO) {
        por(n, TQ_C_FREIO, TQ_FORMA_RETANGULO, W - r - 92 * s - 84 * s, H - b - 104 * s, 84 * s, 104 * s, 16 * s);
        por(n, TQ_C_NITRO, TQ_FORMA_CIRCULO, W - r - 12 * s - 60 * s, H - b - 160 * s - 60 * s, 60 * s, 60 * s, 30 * s);
        por(n, TQ_C_FREIO_MAO, TQ_FORMA_CIRCULO, W - r - 106 * s - 56 * s, H - b - 116 * s - 56 * s, 56 * s, 56 * s,
            28 * s);
        por(n, TQ_C_SPEEDBREAKER, TQ_FORMA_CIRCULO, W - r - 184 * s - 56 * s, H - b - 24 * s - 56 * s, 56 * s,
            56 * s, 28 * s);
    } else {
        /* inclinação (NFSU2): o Freio e o freio de mão sob o polegar esquerdo, o Nitro ao lado do Gás */
        por(n, TQ_C_FREIO, TQ_FORMA_RETANGULO, l, H - b - 104 * s, 84 * s, 104 * s, 16 * s);
        por(n, TQ_C_FREIO_MAO, TQ_FORMA_CIRCULO, l + 96 * s, H - b - 24 * s - 56 * s, 56 * s, 56 * s, 28 * s);
        por(n, TQ_C_NITRO, TQ_FORMA_CIRCULO, W - r - 96 * s - 60 * s, H - b - 24 * s - 60 * s, 60 * s, 60 * s, 30 * s);
        por(n, TQ_C_SPEEDBREAKER, TQ_FORMA_CIRCULO, W - r - 14 * s - 56 * s, H - b - 162 * s - 56 * s, 56 * s,
            56 * s, 28 * s);
    }
    com_texto(n, TQ_C_FREIO, "Freio", 16);
    com_texto(n, TQ_C_NITRO, "N\xE2\x82\x82O", 14); /* N₂O */
    com_texto(n, TQ_C_FREIO_MAO, "Freio\nde m\xC3\xA3o", 12);
    com_texto(n, TQ_C_SPEEDBREAKER, "Speed\nbreaker", 12);
    aplicar_posicoes(n);
}

/* As posições que o usuário deu (tela de agora): tamanho em volta do centro, deslocamento, e nada fora da tela. */
static void aplicar_posicoes(tq_nucleo *n)
{
    /* o deslocamento é guardado em pontos do tamanho 100%: com outro tamanho geral, o arranjo continua o mesmo */
    const int tela = tq_tela_atual(n);
    for (int c = 0; c < TQ_C_COUNT; c++) {
        tq_info_controle *k = &n->c[c];
        if (!k->visivel)
            continue;
        n->padrao_cx[c] = k->cx;
        n->padrao_cy[c] = k->cy;
        tq_posicao *p = &n->pos[tela][c];
        if (!(p->tamanho >= TQ_TAMANHO_MIN && p->tamanho <= TQ_TAMANHO_MAX))
            p->tamanho = 1;
        if (!(fabsf(p->dx) < 1e5f))
            p->dx = 0;
        if (!(fabsf(p->dy) < 1e5f))
            p->dy = 0;
        const float m = p->tamanho;
        k->meia_l *= m;
        k->meia_a *= m;
        k->canto *= m;
        k->letra *= m;
        k->tam_icone *= m;
        const float cx = k->cx + p->dx * n->s, cy = k->cy + p->dy * n->s;
        k->cx = 2 * k->meia_l >= n->w ? n->w / 2 : limitar(cx, k->meia_l, n->w - k->meia_l);
        k->cy = 2 * k->meia_a >= n->h ? n->h / 2 : limitar(cy, k->meia_a, n->h - k->meia_a);
        if (n->editando) {
            /* guarda o que ficou depois de limitar à tela, para o arrasto continuar de onde o controle está */
            p->dx = (k->cx - n->padrao_cx[c]) / n->s;
            p->dy = (k->cy - n->padrao_cy[c]) / n->s;
        }
    }
    if (n->c[TQ_C_ANALOGICO].visivel) {
        /* a área do polegar (NFSU2: 42% da largura e 72% da altura, no canto de baixo à esquerda) acompanha a base */
        const tq_info_controle *k = &n->c[TQ_C_ANALOGICO];
        const float zw = n->w * 0.42f, zh = n->h * 0.72f;
        n->zona_x0 = limitar(k->cx - n->padrao_cx[TQ_C_ANALOGICO], 0, n->w - zw);
        n->zona_x1 = n->zona_x0 + zw;
        n->zona_y0 = limitar(n->h - zh + (k->cy - n->padrao_cy[TQ_C_ANALOGICO]), 0, n->h - zh);
        n->zona_y1 = n->zona_y0 + zh;
        n->trava = TRAVA_ANALOGICO * n->s * n->pos[tela][TQ_C_ANALOGICO].tamanho;
        if (!n->analogico_ativo) {
            n->base_x = n->knob_x = k->cx;
            n->base_y = n->knob_y = k->cy;
        }
    }
}

void tq_layout(tq_nucleo *n, float w, float h, float esquerda, float topo, float direita, float baixo)
{
    n->w = w;
    n->h = h;
    n->se = esquerda;
    n->st = topo;
    n->sd = direita;
    n->sb = baixo;
    refazer_layout(n);
    n->eventos |= TQ_EV_LAYOUT;
}

/* ---------- toques ---------- */

/* O ponto cai no controle? Com folga em volta, e alvo de 44 pontos no mínimo. */
static int contem(const tq_info_controle *k, float x, float y, float folga, float *dist)
{
    const float dx = x - k->cx, dy = y - k->cy;
    *dist = sqrtf(dx * dx + dy * dy);
    if (k->forma == TQ_FORMA_CIRCULO)
        return *dist <= fmaxf(k->meia_l + folga, 22);
    return fabsf(dx) <= fmaxf(k->meia_l + folga, 22) && fabsf(dy) <= fmaxf(k->meia_a + folga, 22);
}

int tq_controle_em(const tq_nucleo *n, float x, float y)
{
    int melhor = -1;
    float melhor_dist = 1e9f;
    for (int i = 0; i < TQ_C_COUNT; i++) {
        const tq_info_controle *k = &n->c[i];
        if (!k->visivel || (i == TQ_C_ANALOGICO && !n->editando))
            continue;
        float d;
        if (contem(k, x, y, 3, &d) && d < melhor_dist) {
            melhor = i;
            melhor_dist = d;
        }
    }
    if (melhor < 0 && !n->editando && n->c[TQ_C_ANALOGICO].visivel && x >= n->zona_x0 && x <= n->zona_x1 &&
        y >= n->zona_y0 && y <= n->zona_y1)
        melhor = TQ_C_ANALOGICO;
    return melhor;
}

static tq_dedo *achar_dedo(tq_nucleo *n, uintptr_t id)
{
    for (int i = 0; i < TQ_MAX_DEDOS; i++)
        if (n->dedos[i].id == id && n->dedos[i].controle >= 0)
            return &n->dedos[i];
    return NULL;
}

static tq_dedo *dedo_livre(tq_nucleo *n)
{
    for (int i = 0; i < TQ_MAX_DEDOS; i++)
        if (n->dedos[i].controle < 0)
            return &n->dedos[i];
    return NULL;
}

/* Os grupos em que o dedo desliza de um controle para outro sem levantar (NFSU2: 'pedal' e 'steer'; o Freio de mão
 * entra nos pedais a pedido, para ir do Gás a ele sem levantar o dedo). */
static int grupo(int c)
{
    if (c == TQ_C_GAS || c == TQ_C_FREIO || c == TQ_C_NITRO || c == TQ_C_FREIO_MAO)
        return 1;
    if (c == TQ_C_ESQUERDA || c == TQ_C_DIREITA)
        return 2;
    return 0;
}

/* O controle do grupo que está debaixo do dedo (-1: nenhum; aí o dedo fica no último). */
static int do_grupo_em(const tq_nucleo *n, int g, float x, float y)
{
    int melhor = -1;
    float melhor_dist = 1e9f;
    for (int i = 0; i < TQ_C_COUNT; i++) {
        float d;
        if (n->c[i].visivel && grupo(i) == g && contem(&n->c[i], x, y, 0, &d) && d < melhor_dist) {
            melhor = i;
            melhor_dist = d;
        }
    }
    return melhor;
}

static void segurar(tq_nucleo *n, int saida, double ate)
{
    if (saida >= 0 && saida < TQ_O_COUNT && n->segurar_ate[saida] < ate)
        n->segurar_ate[saida] = ate;
}

/* A saída que o controle aperta assim que é tocado (-1: nenhuma, ou tratada à parte). */
static int saida_do_controle(const tq_nucleo *n, int c)
{
    switch (c) {
    case TQ_C_DCIMA: return TQ_O_DUP;
    case TQ_C_DBAIXO: return TQ_O_DDOWN;
    case TQ_C_DESQ: return TQ_O_DLEFT;
    case TQ_C_DDIR: return TQ_O_DRIGHT;
    case TQ_C_OK: return TQ_O_A;
    case TQ_C_VOLTAR: return TQ_O_B;
    case TQ_C_X: return TQ_O_X;
    case TQ_C_Y: return TQ_O_Y;
    case TQ_C_LB: return TQ_O_LB;
    case TQ_C_RB: return TQ_O_RB;
    case TQ_C_START: return TQ_O_START;
    case TQ_C_BACK: return TQ_O_BACK;
    case TQ_C_GAS: return TQ_O_RT;
    case TQ_C_FREIO: return TQ_O_LT;
    case TQ_C_FREIO_MAO: return n->aj.mapa[TQ_ACAO_FREIO_MAO];
    case TQ_C_SPEEDBREAKER: return n->aj.mapa[TQ_ACAO_SPEEDBREAKER];
    case TQ_C_PARCEIRO: return n->aj.mapa[TQ_ACAO_PARCEIRO];
    case TQ_C_ENTRAR: return n->aj.mapa[TQ_ACAO_ENTRAR];
    case TQ_C_PAUSA: return n->aj.mapa[TQ_ACAO_PAUSA];
    case TQ_C_RESET: return n->aj.mapa[TQ_ACAO_RESET];
    case TQ_C_MAPA: return n->aj.mapa[TQ_ACAO_MAPA];
    case TQ_C_SMS: return n->aj.mapa[TQ_ACAO_SMS];
    case TQ_C_SOBE: return n->aj.mapa[TQ_ACAO_SOBE];
    case TQ_C_DESCE: return n->aj.mapa[TQ_ACAO_DESCE];
    default: return -1;
    }
}

/* O controle foi apertado (no toque, ou quando um dedo desliza para ele): segura a saída o tempo mínimo. */
static void apertar(tq_nucleo *n, int c, double t)
{
    if (c == TQ_C_NITRO) { /* o Nitro dá gás junto (NFSU2) */
        segurar(n, n->aj.mapa[TQ_ACAO_NITRO], t + APERTO_MINIMO);
        segurar(n, TQ_O_RT, t + APERTO_MINIMO);
    } else {
        segurar(n, saida_do_controle(n, c), t + APERTO_MINIMO);
    }
}

/* ---------- modo de mover ---------- */

/* O gesto no controle c recomeça (entrou ou saiu um dedo): parte da posição de agora e de onde os dedos estão. */
static void recomecar_gesto(tq_nucleo *n, int c)
{
    const tq_dedo *a = NULL, *b = NULL;
    for (int i = 0; i < TQ_MAX_DEDOS; i++) {
        const tq_dedo *d = &n->dedos[i];
        if (d->controle != c)
            continue;
        if (!a)
            a = d;
        else if (!b)
            b = d;
    }
    n->ed_pos0[c] = n->pos[tq_tela_atual(n)][c];
    if (a && b) {
        n->ed_x0[c] = (a->x + b->x) / 2;
        n->ed_y0[c] = (a->y + b->y) / 2;
        n->ed_d0[c] = fmaxf(hypotf(a->x - b->x, a->y - b->y), 1);
    } else if (a) {
        n->ed_x0[c] = a->x;
        n->ed_y0[c] = a->y;
        n->ed_d0[c] = 0;
    }
}

static void editar_moveu(tq_nucleo *n, int c)
{
    const tq_dedo *a = NULL, *b = NULL;
    for (int i = 0; i < TQ_MAX_DEDOS; i++) {
        const tq_dedo *d = &n->dedos[i];
        if (d->controle != c)
            continue;
        if (!a)
            a = d;
        else if (!b)
            b = d;
    }
    if (!a || !n->c[c].visivel)
        return;
    tq_posicao p = n->ed_pos0[c];
    if (b && n->ed_d0[c] > 0) {
        p.dx += ((a->x + b->x) / 2 - n->ed_x0[c]) / n->s;
        p.dy += ((a->y + b->y) / 2 - n->ed_y0[c]) / n->s;
        p.tamanho = limitar(n->ed_pos0[c].tamanho * hypotf(a->x - b->x, a->y - b->y) / n->ed_d0[c], TQ_TAMANHO_MIN,
                            TQ_TAMANHO_MAX);
    } else {
        p.dx += (a->x - n->ed_x0[c]) / n->s;
        p.dy += (a->y - n->ed_y0[c]) / n->s;
    }
    n->pos[tq_tela_atual(n)][c] = p;
    refazer_layout(n);
    n->eventos |= TQ_EV_LAYOUT;
}

void tq_editar(tq_nucleo *n, int ligar)
{
    tq_soltar_tudo(n);
    n->editando = ligar ? 1 : 0;
    n->selecionado = -1;
    refazer_layout(n);
    n->eventos |= TQ_EV_LAYOUT;
}

void tq_ir_para_tela(tq_nucleo *n, int tela)
{
    if (tela < 0 || tela >= TQ_TELAS)
        return;
    tq_soltar_tudo(n);
    const int modo = tela == TQ_TELA_MENU ? TQ_MODO_MENU : TQ_MODO_PILOTAGEM;
    if (modo != n->modo)
        n->eventos |= TQ_EV_MODO;
    n->modo = modo;
    if (tela == TQ_TELA_BOTOES)
        n->aj.direcao = TQ_DIRECAO_BOTOES;
    else if (tela == TQ_TELA_ANALOGICO)
        n->aj.direcao = TQ_DIRECAO_ANALOGICO;
    else if (tela == TQ_TELA_INCLINACAO)
        n->aj.direcao = TQ_DIRECAO_INCLINACAO;
    n->selecionado = -1;
    refazer_layout(n);
    n->eventos |= TQ_EV_LAYOUT;
}

tq_posicao tq_posicao_de(const tq_nucleo *n, int tela, int controle)
{
    tq_posicao p = { 0, 0, 1 };
    if (tela >= 0 && tela < TQ_TELAS && controle >= 0 && controle < TQ_C_COUNT)
        p = n->pos[tela][controle];
    return p;
}

void tq_definir_posicao(tq_nucleo *n, int tela, int controle, tq_posicao p)
{
    if (tela < 0 || tela >= TQ_TELAS || controle < 0 || controle >= TQ_C_COUNT)
        return;
    if (!(p.tamanho >= TQ_TAMANHO_MIN && p.tamanho <= TQ_TAMANHO_MAX))
        p.tamanho = 1;
    if (!(fabsf(p.dx) < 1e5f) || !(fabsf(p.dy) < 1e5f))
        p.dx = p.dy = 0;
    n->pos[tela][controle] = p;
    refazer_layout(n);
    n->eventos |= TQ_EV_LAYOUT;
}

void tq_restaurar_tela(tq_nucleo *n, int tela)
{
    if (tela < 0 || tela >= TQ_TELAS)
        return;
    for (int c = 0; c < TQ_C_COUNT; c++) {
        n->pos[tela][c].dx = n->pos[tela][c].dy = 0;
        n->pos[tela][c].tamanho = 1;
    }
    refazer_layout(n);
    n->eventos |= TQ_EV_LAYOUT;
}

/* ---------- toques ---------- */

void tq_toque_comecou(tq_nucleo *n, uintptr_t id, float x, float y, double t)
{
    if (!id || achar_dedo(n, id))
        return;
    int c = tq_controle_em(n, x, y);
    if (n->editando) {
        /* o segundo dedo, onde cair, faz a pinça no controle que o primeiro segura (os pequenos não cabem dois) */
        int um = -1, quantos = 0;
        for (int i = 0; i < TQ_MAX_DEDOS; i++)
            if (n->dedos[i].controle >= 0) {
                um = n->dedos[i].controle;
                quantos++;
            }
        if (quantos == 1)
            c = um;
    }
    if (c < 0)
        return;
    tq_dedo *d = dedo_livre(n);
    if (!d)
        return;
    memset(d, 0, sizeof(*d));
    d->id = id;
    d->controle = c;
    d->x0 = d->x = x;
    d->y0 = d->y = y;
    d->inicio = t;
    d->atual = c;
    n->eventos |= TQ_EV_TOQUE;

    if (n->editando) {
        n->selecionado = c;
        recomecar_gesto(n, c);
        n->eventos |= TQ_EV_LAYOUT;
        return;
    }
    if (c == TQ_C_ANALOGICO) { /* NFSU2: a base vem para debaixo do polegar */
        n->analogico_ativo = 1;
        n->base_x = n->knob_x = x;
        n->base_y = n->knob_y = y;
    } else {
        apertar(n, c, t);
    }
}

void tq_toque_moveu(tq_nucleo *n, uintptr_t id, float x, float y, double t)
{
    tq_dedo *d = achar_dedo(n, id);
    if (!d)
        return;
    d->x = x;
    d->y = y;
    if (n->editando) {
        editar_moveu(n, d->controle);
        return;
    }
    const int g = grupo(d->controle);
    if (g) {
        /* NFSU2: o dedo passa para outro do grupo quando entra nele; fora de todos, fica no último */
        const int para = do_grupo_em(n, g, x, y);
        if (para >= 0 && para != d->atual) {
            d->atual = para;
            n->eventos |= TQ_EV_TOQUE;
            apertar(n, para, t);
        }
    } else if (d->controle == TQ_C_ANALOGICO) {
        /* NFSU2: o polegar do analógico só anda para os lados, até o fim de curso */
        const float trava = n->trava > 0 ? n->trava : TRAVA_ANALOGICO;
        n->knob_x = n->base_x + limitar(x - n->base_x, -trava, trava);
        n->knob_y = n->base_y;
    }
}

void tq_toque_acabou(tq_nucleo *n, uintptr_t id, double t)
{
    tq_dedo *d = achar_dedo(n, id);
    if (!d)
        return;
    const int c = d->controle;
    const int visivel = n->c[c].visivel;
    const double inicio = d->inicio;
    d->controle = -1;
    d->id = 0;

    if (n->editando) {
        recomecar_gesto(n, c); /* o dedo que ficou continua de onde o controle está */
        return;
    }
    if (c == TQ_C_ANALOGICO) {
        n->analogico_ativo = 0;
        refazer_layout(n); /* a base volta ao lugar de descanso */
        n->eventos |= TQ_EV_LAYOUT;
    } else if (c == TQ_C_CAMERA && visivel) {
        if (t - inicio < SEGURAR_CAMERA)
            segurar(n, n->aj.mapa[TQ_ACAO_CAMERA], t + APERTO_MINIMO); /* toque rápido: trocar a câmera */
    } else if (c == TQ_C_PAUSA && visivel) {
        if (n->aj.pausa_vai_para_menu && n->modo == TQ_MODO_PILOTAGEM)
            tq_definir_modo(n, TQ_MODO_MENU);
    } else if (c == TQ_C_AJUSTES && visivel) {
        n->eventos |= TQ_EV_AJUSTES;
    } else if (c == TQ_C_MODO && visivel) {
        tq_definir_modo(n, n->modo == TQ_MODO_MENU ? TQ_MODO_PILOTAGEM : TQ_MODO_MENU);
    } else if (c == TQ_C_RETO && visivel) {
        n->eventos |= TQ_EV_RETO;
    } else if (c == TQ_C_DIRECAO && visivel) {
        /* NFSU2: o botão passa por botões, analógico e inclinação; as setas, os pedais e o analógico são soltos (o
         * Freio muda de lado na inclinação) */
        for (int i = 0; i < TQ_MAX_DEDOS; i++)
            if (grupo(n->dedos[i].controle) || n->dedos[i].controle == TQ_C_ANALOGICO) {
                n->dedos[i].controle = -1;
                n->dedos[i].id = 0;
            }
        n->aj.direcao = (n->aj.direcao + 1) % 3;
        n->analogico_ativo = 0;
        n->volante = 0;
        refazer_layout(n);
        n->eventos |= TQ_EV_DIRECAO | TQ_EV_LAYOUT;
    }
}

void tq_soltar_tudo(tq_nucleo *n)
{
    for (int i = 0; i < TQ_MAX_DEDOS; i++) {
        n->dedos[i].controle = -1;
        n->dedos[i].id = 0;
    }
    for (int i = 0; i < TQ_O_COUNT; i++)
        n->segurar_ate[i] = 0;
    n->volante = 0;
    if (n->analogico_ativo) {
        n->analogico_ativo = 0;
        refazer_layout(n);
        n->eventos |= TQ_EV_LAYOUT;
    }
}

/* ---------- inclinação ---------- */

float tq_inclinacao_relativa(float angulo, float reto)
{
    float d = angulo - reto;
    while (d > 180)
        d -= 360;
    while (d < -180)
        d += 360;
    return d;
}

void tq_definir_inclinacao(tq_nucleo *n, float graus_relativos) { n->inclinacao_graus = graus_relativos; }

void tq_pulso(tq_nucleo *n, uint32_t saidas, double t, double duracao)
{
    for (int i = 0; i < TQ_O_COUNT; i++)
        if (saidas & (1u << i))
            segurar(n, i, t + duracao);
}

/* ---------- a cada quadro ---------- */

void tq_tick(tq_nucleo *n, double t, tq_estado_controle *s)
{
    double dt = n->tick_inicial ? 0 : t - n->ultimo_tick;
    if (dt < 0)
        dt = 0;
    if (dt > 1.0 / 30)
        dt = 1.0 / 30; /* depois de uma pausa (camada escondida, app em segundo plano), sem pulo na rampa */
    n->ultimo_tick = t;
    n->tick_inicial = 0;
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < TQ_C_COUNT; i++)
        n->c[i].apertado = 0;
    if (n->editando) { /* movendo os botões: nada vai para o jogo */
        n->volante = 0;
        return;
    }

    int ativo[TQ_O_COUNT];
    for (int i = 0; i < TQ_O_COUNT; i++)
        ativo[i] = t < n->segurar_ate[i];

    int esquerda = 0, direita = 0;
    float analogico = 0;
    for (int i = 0; i < TQ_MAX_DEDOS; i++) {
        const tq_dedo *d = &n->dedos[i];
        if (d->controle < 0 || !n->c[d->controle].visivel)
            continue;
        const int c = d->controle;
        if (grupo(c)) {
            const int p = d->atual;
            if (!n->c[p].visivel)
                continue;
            n->c[p].apertado = 1;
            if (p == TQ_C_GAS)
                ativo[TQ_O_RT] = 1;
            else if (p == TQ_C_FREIO)
                ativo[TQ_O_LT] = 1;
            else if (p == TQ_C_NITRO) {
                ativo[TQ_O_RT] = 1;
                ativo[n->aj.mapa[TQ_ACAO_NITRO]] = 1;
            } else if (p == TQ_C_FREIO_MAO)
                ativo[n->aj.mapa[TQ_ACAO_FREIO_MAO]] = 1;
            else if (p == TQ_C_ESQUERDA)
                esquerda = 1;
            else if (p == TQ_C_DIREITA)
                direita = 1;
            continue;
        }
        n->c[c].apertado = 1;
        if (c == TQ_C_ANALOGICO) {
            const float trava = n->trava > 0 ? n->trava : TRAVA_ANALOGICO;
            analogico = limitar((d->x - n->base_x) / trava, -1, 1);
        } else if (c == TQ_C_CAMERA) {
            if (t - d->inicio >= SEGURAR_CAMERA)
                ativo[n->aj.mapa[TQ_ACAO_OLHAR_TRAS]] = 1;
        } else {
            const int o = saida_do_controle(n, c);
            if (o >= 0)
                ativo[o] = 1;
        }
    }

    float volante = 0;
    if (n->modo == TQ_MODO_PILOTAGEM) {
        if (n->aj.direcao == TQ_DIRECAO_BOTOES) {
            const float alvo = (float)(direita - esquerda);
            const float atual = n->volante;
            const int voltando = fabsf(alvo) < fabsf(atual) || (alvo * atual < 0);
            const float passo = (float)(dt / (voltando ? RAMPA_VOLTA : RAMPA_IDA));
            if (atual < alvo)
                volante = atual + passo > alvo ? alvo : atual + passo;
            else
                volante = atual - passo < alvo ? alvo : atual - passo;
        } else if (n->aj.direcao == TQ_DIRECAO_ANALOGICO) {
            volante = analogico;
        } else {
            /* NFSU2: 30 graus até o fim de curso, um pouco suavizado contra o tremor do sensor (lá, 40% do valor
             * anterior a cada leitura do sensor, 60 por segundo; aqui pelo tempo, igual em telas de 120 Hz) */
            const float alvo = limitar(n->inclinacao_graus / INCLINACAO_FIM, -1, 1);
            const float fica = powf(0.4f, (float)(dt * 60));
            volante = n->volante * fica + alvo * (1 - fica);
            if (fabsf(volante - alvo) < 1e-4f)
                volante = alvo;
        }
    }
    n->volante = volante;

    for (int i = 0; i < TQ_O_BOTOES; i++)
        if (ativo[i])
            s->botoes |= 1u << i;
    s->lx = volante;
    s->ly = 0;
    s->rx = (float)(ativo[TQ_O_RRIGHT] - ativo[TQ_O_RLEFT]);
    s->ry = (float)(ativo[TQ_O_RDOWN] - ativo[TQ_O_RUP]); /* SDL: para cima é negativo */
    s->lt = ativo[TQ_O_LT] ? 1.0f : 0.0f;
    s->rt = ativo[TQ_O_RT] ? 1.0f : 0.0f;
}

uint32_t tq_eventos(tq_nucleo *n)
{
    const uint32_t e = n->eventos;
    n->eventos = 0;
    return e;
}

/* ---------- nomes ---------- */

static const char *const IDS[TQ_C_COUNT] = {
    [TQ_C_DCIMA] = "dcima", [TQ_C_DBAIXO] = "dbaixo", [TQ_C_DESQ] = "desq", [TQ_C_DDIR] = "ddir",
    [TQ_C_OK] = "ok", [TQ_C_VOLTAR] = "voltar", [TQ_C_X] = "x", [TQ_C_Y] = "y", [TQ_C_LB] = "lb", [TQ_C_RB] = "rb",
    [TQ_C_START] = "start", [TQ_C_BACK] = "back", [TQ_C_ESQUERDA] = "esquerda", [TQ_C_DIREITA] = "direita",
    [TQ_C_ANALOGICO] = "analogico", [TQ_C_RETO] = "reto", [TQ_C_DIRECAO] = "direcao", [TQ_C_GAS] = "gas",
    [TQ_C_FREIO] = "freio", [TQ_C_NITRO] = "nitro", [TQ_C_FREIO_MAO] = "freio_mao",
    [TQ_C_SPEEDBREAKER] = "speedbreaker", [TQ_C_PARCEIRO] = "equipe", [TQ_C_CAMERA] = "camera",
    [TQ_C_ENTRAR] = "entrar", [TQ_C_PAUSA] = "pausa", [TQ_C_RESET] = "reset", [TQ_C_MAPA] = "mapa",
    [TQ_C_SMS] = "sms", [TQ_C_SOBE] = "sobe", [TQ_C_DESCE] = "desce", [TQ_C_MODO] = "modo",
    [TQ_C_AJUSTES] = "ajustes",
};

static const char *const NOMES[TQ_C_COUNT] = {
    [TQ_C_DCIMA] = "Direcional \xE2\x86\x91", [TQ_C_DBAIXO] = "Direcional \xE2\x86\x93",
    [TQ_C_DESQ] = "Direcional \xE2\x86\x90", [TQ_C_DDIR] = "Direcional \xE2\x86\x92", [TQ_C_OK] = "OK",
    [TQ_C_VOLTAR] = "Voltar", [TQ_C_X] = "X", [TQ_C_Y] = "Y", [TQ_C_LB] = "LB", [TQ_C_RB] = "RB",
    [TQ_C_START] = "Start", [TQ_C_BACK] = "Back", [TQ_C_ESQUERDA] = "Virar \xC3\xA0 esquerda",
    [TQ_C_DIREITA] = "Virar \xC3\xA0 direita", [TQ_C_ANALOGICO] = "Anal\xC3\xB3gico", [TQ_C_RETO] = "Reto",
    [TQ_C_DIRECAO] = "Jeito de dirigir", [TQ_C_GAS] = "G\xC3\xA1s", [TQ_C_FREIO] = "Freio", [TQ_C_NITRO] = "Nitro",
    [TQ_C_FREIO_MAO] = "Freio de m\xC3\xA3o", [TQ_C_SPEEDBREAKER] = "Speedbreaker", [TQ_C_PARCEIRO] = "Equipe",
    [TQ_C_CAMERA] = "C\xC3\xA2mera", [TQ_C_ENTRAR] = "Entrar", [TQ_C_PAUSA] = "Pausa", [TQ_C_RESET] = "Reset",
    [TQ_C_MAPA] = "Mapa", [TQ_C_SMS] = "SMS", [TQ_C_SOBE] = "Marcha +", [TQ_C_DESCE] = "Marcha \xE2\x88\x92",
    [TQ_C_MODO] = "Menu e Pilotar", [TQ_C_AJUSTES] = "Ajustes",
};

static const char *const IDS_TELA[TQ_TELAS] = { "menu", "botoes", "analogico", "inclinacao" };
static const char *const NOMES_TELA[TQ_TELAS] = { "Menu", "Pilotar com bot\303\265es",
                                                  "Pilotar com o anal\xC3\xB3gico", "Pilotar inclinando" };

const char *tq_id_controle(int c) { return c >= 0 && c < TQ_C_COUNT && IDS[c] ? IDS[c] : "?"; }
const char *tq_nome_controle(int c) { return c >= 0 && c < TQ_C_COUNT && NOMES[c] ? NOMES[c] : "?"; }
const char *tq_id_tela(int tela) { return tela >= 0 && tela < TQ_TELAS ? IDS_TELA[tela] : "?"; }
const char *tq_nome_tela(int tela) { return tela >= 0 && tela < TQ_TELAS ? NOMES_TELA[tela] : "?"; }

int tq_controle_por_id(const char *id)
{
    for (int c = 0; id && c < TQ_C_COUNT; c++)
        if (IDS[c] && strcmp(IDS[c], id) == 0)
            return c;
    return -1;
}

int tq_tela_por_id(const char *id)
{
    for (int t = 0; id && t < TQ_TELAS; t++)
        if (strcmp(IDS_TELA[t], id) == 0)
            return t;
    return -1;
}

const char *tq_nome_saida(int saida)
{
    static const char *const nomes[TQ_O_COUNT] = {
        "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LS (clique)", "RS (clique)",
        "Direcional \xE2\x86\x91", "Direcional \xE2\x86\x93", "Direcional \xE2\x86\x90", "Direcional \xE2\x86\x92",
        "LT", "RT",
        "Anal\xC3\xB3gico direito \xE2\x86\x91", "Anal\xC3\xB3gico direito \xE2\x86\x93",
        "Anal\xC3\xB3gico direito \xE2\x86\x90", "Anal\xC3\xB3gico direito \xE2\x86\x92",
    };
    return saida >= 0 && saida < TQ_O_COUNT ? nomes[saida] : "?";
}

const char *tq_nome_acao(int acao)
{
    static const char *const nomes[TQ_ACAO_COUNT] = {
        "Freio de m\xC3\xA3o", "Nitro", "Speedbreaker", "Equipe (ativar o parceiro)", "Reset do carro", "C\xC3\xA2mera",
        "Olhar para tr\xC3\xA1s (segurar a c\xC3\xA2mera)", "Pausa", "Entrar na corrida ou loja", "Mapa", "SMS",
        "Marcha +", "Marcha \xE2\x88\x92",
    };
    return acao >= 0 && acao < TQ_ACAO_COUNT ? nomes[acao] : "?";
}
