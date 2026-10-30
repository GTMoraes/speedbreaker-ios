/*
 * Toque: núcleo em C dos controles por toque (ver nucleo.h).
 */
#include "nucleo.h"

#include <math.h>
#include <string.h>

#define APERTO_MINIMO 0.10      /* s: um toque rápido segura o botão pelo menos isso (o jogo lê uma vez por quadro) */
#define SEGURAR_CAMERA 0.25     /* s: Câmera segurada vira "olhar para trás" */
#define RAMPA_IDA 0.18          /* s: botões de direção, do centro ao fim de curso */
#define RAMPA_VOLTA 0.08        /* s: e de volta ao centro */
#define INCLINACAO_FIM 30.0f    /* graus de inclinação para o fim de curso */
#define INCLINACAO_ZONA 1.5f    /* graus perto do reto que contam como reto */

static float limitar(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

void tq_ajustes_padrao(tq_ajustes *aj)
{
    memset(aj, 0, sizeof(*aj));
    aj->direcao = TQ_DIRECAO_BOTOES;
    aj->escala = 1.0f;
    aj->cambio_manual = 0;
    aj->pausa_vai_para_menu = 1;
    /* Controles do manual do Carbon de Xbox 360. Mapa, SMS e as marchas: a confirmar no jogo. */
    aj->mapa[TQ_ACAO_FREIO_MAO] = TQ_O_A;
    aj->mapa[TQ_ACAO_NITRO] = TQ_O_B;
    aj->mapa[TQ_ACAO_SPEEDBREAKER] = TQ_O_X;
    aj->mapa[TQ_ACAO_PARCEIRO] = TQ_O_Y;
    aj->mapa[TQ_ACAO_RESET] = TQ_O_LB;
    aj->mapa[TQ_ACAO_CAMERA] = TQ_O_RB;
    aj->mapa[TQ_ACAO_OLHAR_TRAS] = TQ_O_RS;
    aj->mapa[TQ_ACAO_PAUSA] = TQ_O_START;
    aj->mapa[TQ_ACAO_ENTRAR] = TQ_O_BACK;
    aj->mapa[TQ_ACAO_MAPA] = TQ_O_DUP;
    aj->mapa[TQ_ACAO_SMS] = TQ_O_DDOWN;
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
    n->dpad_dir = -1;
    n->tick_inicial = 1;
    for (int i = 0; i < TQ_MAX_DEDOS; i++)
        n->dedos[i].controle = -1;
}

void tq_definir_ajustes(tq_nucleo *n, const tq_ajustes *aj)
{
    n->aj = *aj;
    corrigir_ajustes(&n->aj);
    refazer_layout(n);
}

void tq_definir_modo(tq_nucleo *n, int modo)
{
    modo = modo == TQ_MODO_PILOTAGEM ? TQ_MODO_PILOTAGEM : TQ_MODO_MENU;
    if (n->modo == modo)
        return;
    n->modo = modo;
    n->mais_aberto = 0;
    n->eventos |= TQ_EV_MODO | TQ_EV_LAYOUT;
    refazer_layout(n);
}

/* ---------- posições ---------- */

static void pos(tq_nucleo *n, int c, tq_forma forma, float cx, float cy, float r, float meia_l, const char *rotulo)
{
    tq_info_controle *k = &n->c[c];
    k->visivel = 1;
    k->forma = forma;
    k->cx = cx;
    k->cy = cy;
    k->r = r;
    k->meia_l = meia_l;
    k->rotulo = rotulo;
}

static void refazer_layout(tq_nucleo *n)
{
    for (int i = 0; i < TQ_C_COUNT; i++)
        n->c[i].visivel = 0;
    if (n->w <= 0 || n->h <= 0)
        return;
    const float x0 = n->se, x1 = n->w - n->sd, y0 = n->st, y1 = n->h - n->sb;
    /* unidade: 1% da altura útil, vezes o tamanho escolhido. Com o tamanho grande, o layout precisa de ~74 unidades
     * de altura (fileira de cima + controles de baixo) e ~130 de largura: a unidade não passa disso. */
    float u = (y1 - y0) * n->aj.escala / 100.0f;
    if (u > (y1 - y0) / 74.0f)
        u = (y1 - y0) / 74.0f;
    if (u > (x1 - x0) / 130.0f)
        u = (x1 - x0) / 130.0f;
    n->u = u;
    const float topo = y0 + 6 * u;

    pos(n, TQ_C_MODO, TQ_FORMA_RETANGULO, n->w / 2, topo, 4 * u, 11 * u,
        n->modo == TQ_MODO_MENU ? "PILOTAR" : "MENU");
    pos(n, TQ_C_AJUSTES, TQ_FORMA_CIRCULO, x1 - 6 * u, topo, 4.5f * u, 0, "\xE2\x9A\x99"); /* ⚙ */

    if (n->modo == TQ_MODO_MENU) {
        pos(n, TQ_C_DPAD, TQ_FORMA_CIRCULO, x0 + 24 * u, y1 - 26 * u, 17 * u, 0, "");
        const float cx = x1 - 27 * u, cy = y1 - 27 * u, d = 13 * u;
        pos(n, TQ_C_OK, TQ_FORMA_CIRCULO, cx, cy + d, 8 * u, 0, "OK");
        pos(n, TQ_C_VOLTAR, TQ_FORMA_CIRCULO, cx + d, cy, 8 * u, 0, "VOLTAR");
        pos(n, TQ_C_X, TQ_FORMA_CIRCULO, cx - d, cy, 7 * u, 0, "X");
        pos(n, TQ_C_Y, TQ_FORMA_CIRCULO, cx, cy - d, 7 * u, 0, "Y");
        pos(n, TQ_C_LB, TQ_FORMA_RETANGULO, x0 + 12 * u, y0 + 20 * u, 4.5f * u, 9 * u, "LB");
        pos(n, TQ_C_RB, TQ_FORMA_RETANGULO, x1 - 12 * u, y0 + 20 * u, 4.5f * u, 9 * u, "RB");
        pos(n, TQ_C_BACK, TQ_FORMA_RETANGULO, n->w / 2 - 26 * u, topo, 4 * u, 7 * u, "BACK");
        pos(n, TQ_C_START, TQ_FORMA_RETANGULO, n->w / 2 + 26 * u, topo, 4 * u, 7 * u, "START");
        return;
    }

    const int inclinacao = n->aj.direcao == TQ_DIRECAO_INCLINACAO;
    if (n->aj.direcao == TQ_DIRECAO_BOTOES) {
        pos(n, TQ_C_ESQUERDA, TQ_FORMA_CIRCULO, x0 + 14 * u, y1 - 16 * u, 11 * u, 0, "\xE2\x97\x80"); /* ◀ */
        pos(n, TQ_C_DIREITA, TQ_FORMA_CIRCULO, x0 + 40 * u, y1 - 16 * u, 11 * u, 0, "\xE2\x96\xB6"); /* ▶ */
    } else if (n->aj.direcao == TQ_DIRECAO_ANALOGICO) {
        /* a área do polegar: metade esquerda, abaixo da fileira de cima */
        const float zx0 = 0, zx1 = n->w * 0.45f, zy0 = y0 + 12 * u, zy1 = n->h;
        pos(n, TQ_C_ANALOGICO, TQ_FORMA_RETANGULO, (zx0 + zx1) / 2, (zy0 + zy1) / 2, (zy1 - zy0) / 2,
            (zx1 - zx0) / 2, "");
        if (!n->analogico_ativo) {
            n->base_x = n->knob_x = x0 + 24 * u;
            n->base_y = n->knob_y = y1 - 22 * u;
        }
    }

    if (inclinacao) {
        pos(n, TQ_C_FREIO, TQ_FORMA_CIRCULO, x0 + 14 * u, y1 - 16 * u, 11 * u, 0, "FREIO");
        pos(n, TQ_C_FREIO_MAO, TQ_FORMA_CIRCULO, x0 + 39 * u, y1 - 14 * u, 9 * u, 0, "FREIO\nDE M\xC3\x83O");
        pos(n, TQ_C_GAS, TQ_FORMA_CIRCULO, x1 - 15 * u, y1 - 17 * u, 12 * u, 0, "G\xC3\x81S");
        pos(n, TQ_C_NITRO, TQ_FORMA_CIRCULO, x1 - 15 * u, y1 - 44 * u, 9 * u, 0, "NITRO");
        pos(n, TQ_C_SPEEDBREAKER, TQ_FORMA_CIRCULO, x1 - 40 * u, y1 - 15 * u, 9 * u, 0, "SPEED\nBREAKER");
        pos(n, TQ_C_PARCEIRO, TQ_FORMA_CIRCULO, x1 - 40 * u, y1 - 38 * u, 7.5f * u, 0, "PARCEIRO");
    } else {
        pos(n, TQ_C_GAS, TQ_FORMA_CIRCULO, x1 - 15 * u, y1 - 17 * u, 12 * u, 0, "G\xC3\x81S");
        pos(n, TQ_C_FREIO, TQ_FORMA_CIRCULO, x1 - 41 * u, y1 - 13 * u, 10 * u, 0, "FREIO");
        pos(n, TQ_C_NITRO, TQ_FORMA_CIRCULO, x1 - 15 * u, y1 - 44 * u, 9 * u, 0, "NITRO");
        pos(n, TQ_C_FREIO_MAO, TQ_FORMA_CIRCULO, x1 - 41 * u, y1 - 37 * u, 8.5f * u, 0, "FREIO\nDE M\xC3\x83O");
        pos(n, TQ_C_SPEEDBREAKER, TQ_FORMA_CIRCULO, x1 - 64 * u, y1 - 22 * u, 8 * u, 0, "SPEED\nBREAKER");
        pos(n, TQ_C_PARCEIRO, TQ_FORMA_CIRCULO, x1 - 64 * u, y1 - 42 * u, 7 * u, 0, "PARCEIRO");
    }

    if (n->aj.cambio_manual) {
        pos(n, TQ_C_DESCE, TQ_FORMA_CIRCULO, x0 + 14 * u, y1 - 40 * u, 7 * u, 0, "M\xE2\x88\x92"); /* M− */
        pos(n, TQ_C_SOBE, TQ_FORMA_CIRCULO, x0 + 39 * u, y1 - 36 * u, 7 * u, 0, "M+");
    }

    /* fileira de cima */
    pos(n, TQ_C_CAMERA, TQ_FORMA_CIRCULO, x0 + 7 * u, topo, 5 * u, 0, "C\xC3\x82M");
    pos(n, TQ_C_ENTRAR, TQ_FORMA_CIRCULO, x0 + 20 * u, topo, 5 * u, 0, "ENTRAR");
    pos(n, TQ_C_MAIS, TQ_FORMA_CIRCULO, x0 + 33 * u, topo, 5 * u, 0, n->mais_aberto ? "\xE2\x88\x92" : "+");
    if (inclinacao)
        pos(n, TQ_C_RETO, TQ_FORMA_CIRCULO, x0 + 46 * u, topo, 5 * u, 0, "RETO");
    pos(n, TQ_C_PAUSA, TQ_FORMA_CIRCULO, x1 - 19 * u, topo, 5 * u, 0, "\xE2\x9D\x9A\xE2\x9D\x9A"); /* ❚❚ */
    if (n->mais_aberto) {
        const float linha2 = topo + 12 * u;
        pos(n, TQ_C_RESET, TQ_FORMA_CIRCULO, x0 + 7 * u, linha2, 5 * u, 0, "RESET");
        pos(n, TQ_C_MAPA, TQ_FORMA_CIRCULO, x0 + 20 * u, linha2, 5 * u, 0, "MAPA");
        pos(n, TQ_C_SMS, TQ_FORMA_CIRCULO, x0 + 33 * u, linha2, 5 * u, 0, "SMS");
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

static int dentro(const tq_info_controle *k, float x, float y, float *dist)
{
    const float dx = x - k->cx, dy = y - k->cy;
    *dist = sqrtf(dx * dx + dy * dy);
    if (k->forma == TQ_FORMA_CIRCULO) {
        float alcance = k->r * 1.15f;
        if (alcance < 22)
            alcance = 22; /* alvo de toque de pelo menos 44 pontos */
        return *dist <= alcance;
    }
    const float mx = k->meia_l + 3, my = k->r + 3;
    return fabsf(dx) <= mx && fabsf(dy) <= my;
}

int tq_controle_em(const tq_nucleo *n, float x, float y)
{
    int melhor = -1;
    float melhor_dist = 1e9f;
    for (int i = 0; i < TQ_C_COUNT; i++) {
        const tq_info_controle *k = &n->c[i];
        if (!k->visivel || i == TQ_C_ANALOGICO)
            continue;
        float d;
        if (dentro(k, x, y, &d) && d < melhor_dist) {
            melhor = i;
            melhor_dist = d;
        }
    }
    if (melhor < 0 && n->c[TQ_C_ANALOGICO].visivel) {
        float d;
        if (dentro(&n->c[TQ_C_ANALOGICO], x, y, &d))
            melhor = TQ_C_ANALOGICO;
    }
    return melhor;
}

static tq_dedo *achar_dedo(tq_nucleo *n, uintptr_t id)
{
    for (int i = 0; i < TQ_MAX_DEDOS; i++)
        if (n->dedos[i].id == id && n->dedos[i].controle >= 0)
            return &n->dedos[i];
    return NULL;
}

static int e_pedal(int c) { return c == TQ_C_GAS || c == TQ_C_FREIO || c == TQ_C_NITRO; }
static int e_seta(int c) { return c == TQ_C_ESQUERDA || c == TQ_C_DIREITA; }

/* Pedais e setas: o dedo desliza de um para outro sem levantar. Fora de todos, fica no último. */
static int grupo_mais_perto(const tq_nucleo *n, int atual, float x, float y)
{
    int melhor = atual;
    float melhor_dist = 1e9f;
    for (int i = 0; i < TQ_C_COUNT; i++) {
        if (!n->c[i].visivel)
            continue;
        if (e_pedal(atual) ? !e_pedal(i) : !e_seta(i))
            continue;
        const float dx = x - n->c[i].cx, dy = y - n->c[i].cy;
        const float d = sqrtf(dx * dx + dy * dy);
        if (d <= n->c[i].r * 1.8f && d < melhor_dist) {
            melhor = i;
            melhor_dist = d;
        }
    }
    return melhor;
}

static void dpad_atualizar(tq_nucleo *n, float x, float y)
{
    const tq_info_controle *k = &n->c[TQ_C_DPAD];
    const float dx = x - k->cx, dy = y - k->cy;
    if (sqrtf(dx * dx + dy * dy) < k->r * 0.25f) {
        n->dpad_dir = -1;
        return;
    }
    if (fabsf(dx) > fabsf(dy))
        n->dpad_dir = dx > 0 ? TQ_O_DRIGHT : TQ_O_DLEFT;
    else
        n->dpad_dir = dy > 0 ? TQ_O_DDOWN : TQ_O_DUP;
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

void tq_toque_comecou(tq_nucleo *n, uintptr_t id, float x, float y, double t)
{
    if (!id || achar_dedo(n, id))
        return;
    const int c = tq_controle_em(n, x, y);
    if (c < 0)
        return;
    tq_dedo *d = NULL;
    for (int i = 0; i < TQ_MAX_DEDOS; i++)
        if (n->dedos[i].controle < 0) {
            d = &n->dedos[i];
            break;
        }
    if (!d)
        return;
    memset(d, 0, sizeof(*d));
    d->id = id;
    d->controle = c;
    d->x0 = d->x = x;
    d->y0 = d->y = y;
    d->inicio = t;
    d->pedal = c;
    n->eventos |= TQ_EV_TOQUE;

    if (c == TQ_C_ANALOGICO) {
        n->analogico_ativo = 1;
        n->base_x = n->knob_x = x;
        n->base_y = n->knob_y = y;
    } else if (c == TQ_C_DPAD) {
        dpad_atualizar(n, x, y);
        if (n->dpad_dir >= 0)
            segurar(n, n->dpad_dir, t + APERTO_MINIMO);
    } else if (c == TQ_C_MODO) {
        tq_definir_modo(n, n->modo == TQ_MODO_MENU ? TQ_MODO_PILOTAGEM : TQ_MODO_MENU);
    } else if (c == TQ_C_RETO) {
        n->eventos |= TQ_EV_RETO;
    } else if (c == TQ_C_NITRO) {
        segurar(n, n->aj.mapa[TQ_ACAO_NITRO], t + APERTO_MINIMO);
        segurar(n, TQ_O_RT, t + APERTO_MINIMO);
    } else {
        segurar(n, saida_do_controle(n, c), t + APERTO_MINIMO);
    }
}

void tq_toque_moveu(tq_nucleo *n, uintptr_t id, float x, float y, double t)
{
    tq_dedo *d = achar_dedo(n, id);
    if (!d)
        return;
    d->x = x;
    d->y = y;
    if (e_pedal(d->controle) || e_seta(d->controle)) {
        const int novo = grupo_mais_perto(n, d->pedal, x, y);
        if (novo != d->pedal) {
            d->pedal = novo;
            n->eventos |= TQ_EV_TOQUE;
            if (novo == TQ_C_NITRO) {
                segurar(n, n->aj.mapa[TQ_ACAO_NITRO], t + APERTO_MINIMO);
                segurar(n, TQ_O_RT, t + APERTO_MINIMO);
            } else {
                segurar(n, saida_do_controle(n, novo), t + APERTO_MINIMO);
            }
        }
    } else if (d->controle == TQ_C_ANALOGICO) {
        n->knob_x = x;
        n->knob_y = y;
    } else if (d->controle == TQ_C_DPAD) {
        const int antes = n->dpad_dir;
        dpad_atualizar(n, x, y);
        if (n->dpad_dir != antes && n->dpad_dir >= 0) {
            segurar(n, n->dpad_dir, t + APERTO_MINIMO);
            n->eventos |= TQ_EV_TOQUE;
        }
    }
}

void tq_toque_acabou(tq_nucleo *n, uintptr_t id, double t)
{
    tq_dedo *d = achar_dedo(n, id);
    if (!d)
        return;
    const int c = d->controle;
    const int visivel = n->c[c].visivel;
    d->controle = -1;
    d->id = 0;

    if (c == TQ_C_ANALOGICO) {
        n->analogico_ativo = 0;
        n->base_x = n->knob_x;
        n->base_y = n->knob_y;
        refazer_layout(n); /* o analógico volta ao lugar de descanso */
        n->eventos |= TQ_EV_LAYOUT;
    } else if (c == TQ_C_DPAD) {
        n->dpad_dir = -1;
    } else if (c == TQ_C_CAMERA && visivel) {
        if (t - d->inicio < SEGURAR_CAMERA)
            segurar(n, n->aj.mapa[TQ_ACAO_CAMERA], t + APERTO_MINIMO); /* toque rápido: trocar a câmera */
    } else if (c == TQ_C_PAUSA && visivel) {
        if (n->aj.pausa_vai_para_menu && n->modo == TQ_MODO_PILOTAGEM)
            tq_definir_modo(n, TQ_MODO_MENU);
    } else if (c == TQ_C_MAIS && visivel) {
        n->mais_aberto = !n->mais_aberto;
        refazer_layout(n);
        n->eventos |= TQ_EV_LAYOUT;
    } else if ((c == TQ_C_RESET || c == TQ_C_MAPA || c == TQ_C_SMS) && visivel) {
        n->mais_aberto = 0;
        refazer_layout(n);
        n->eventos |= TQ_EV_LAYOUT;
    } else if (c == TQ_C_AJUSTES && visivel) {
        n->eventos |= TQ_EV_AJUSTES;
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
    n->dpad_dir = -1;
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

    int ativo[TQ_O_COUNT];
    for (int i = 0; i < TQ_O_COUNT; i++)
        ativo[i] = t < n->segurar_ate[i];
    for (int i = 0; i < TQ_C_COUNT; i++)
        n->c[i].apertado = 0;

    int esquerda = 0, direita = 0;
    float analogico = 0;
    for (int i = 0; i < TQ_MAX_DEDOS; i++) {
        const tq_dedo *d = &n->dedos[i];
        if (d->controle < 0 || !n->c[d->controle].visivel)
            continue;
        const int c = d->controle;
        if (e_pedal(c) || e_seta(c)) {
            const int p = d->pedal;
            n->c[p].apertado = 1;
            if (p == TQ_C_GAS)
                ativo[TQ_O_RT] = 1;
            else if (p == TQ_C_FREIO)
                ativo[TQ_O_LT] = 1;
            else if (p == TQ_C_NITRO) {
                ativo[TQ_O_RT] = 1;
                ativo[n->aj.mapa[TQ_ACAO_NITRO]] = 1;
            } else if (p == TQ_C_ESQUERDA)
                esquerda = 1;
            else if (p == TQ_C_DIREITA)
                direita = 1;
            continue;
        }
        n->c[c].apertado = 1;
        if (c == TQ_C_DPAD) {
            if (n->dpad_dir >= 0)
                ativo[n->dpad_dir] = 1;
        } else if (c == TQ_C_ANALOGICO) {
            const float raio = n->u > 0 ? 12 * n->u : 30;
            analogico = limitar((d->x - n->base_x) / raio, -1, 1);
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
            float g = n->inclinacao_graus;
            if (fabsf(g) < INCLINACAO_ZONA)
                g = 0;
            volante = limitar(g / INCLINACAO_FIM, -1, 1);
        }
    }
    n->volante = volante;

    memset(s, 0, sizeof(*s));
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
        "Freio de m\xC3\xA3o", "Nitro", "Speedbreaker", "Parceiro", "Reset do carro", "C\xC3\xA2mera",
        "Olhar para tr\xC3\xA1s (segurar C\xC3\x82M)", "Pausa", "Entrar na corrida ou loja", "Mapa", "SMS",
        "Marcha +", "Marcha \xE2\x88\x92",
    };
    return acao >= 0 && acao < TQ_ACAO_COUNT ? nomes[acao] : "?";
}
