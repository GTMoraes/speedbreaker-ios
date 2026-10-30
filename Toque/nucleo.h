/*
 * Toque: controles por toque para o NFS Carbon (SpeedBreaker). Núcleo em C, sem nada de iOS:
 * as posições e os tamanhos dos controles (os do port do NFSU2, em pontos, com os botões que só o Carbon tem), os
 * toques, os modos, o modo de mover os botões (arrastar e pinça) e o que cada toque aperta no controle de Xbox 360
 * virtual. A parte de iOS (Toque.m) só desenha, repassa os toques e manda o resultado para o SDL (ponte_sdl.c).
 * Testado em testes/testar_nucleo.c.
 */
#ifndef TOQUE_NUCLEO_H
#define TOQUE_NUCLEO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* O que a camada aperta no controle virtual (um controle de Xbox 360). */
typedef enum {
    TQ_O_A, TQ_O_B, TQ_O_X, TQ_O_Y,
    TQ_O_LB, TQ_O_RB, TQ_O_BACK, TQ_O_START, TQ_O_LS, TQ_O_RS,
    TQ_O_DUP, TQ_O_DDOWN, TQ_O_DLEFT, TQ_O_DRIGHT,
    TQ_O_LT, TQ_O_RT,                              /* gatilhos, apertados até o fim */
    TQ_O_RUP, TQ_O_RDOWN, TQ_O_RLEFT, TQ_O_RRIGHT, /* analógico direito empurrado até o fim */
    TQ_O_COUNT
} tq_saida;
#define TQ_O_BOTOES 14 /* TQ_O_A..TQ_O_DRIGHT são botões digitais (bits de tq_estado_controle.botoes) */

/* Ações da pilotagem que podem ser reatribuídas (cada uma aperta uma tq_saida). */
typedef enum {
    TQ_ACAO_FREIO_MAO, TQ_ACAO_NITRO, TQ_ACAO_SPEEDBREAKER, TQ_ACAO_PARCEIRO,
    TQ_ACAO_RESET, TQ_ACAO_CAMERA, TQ_ACAO_OLHAR_TRAS, TQ_ACAO_PAUSA, TQ_ACAO_ENTRAR,
    TQ_ACAO_MAPA, TQ_ACAO_SMS, TQ_ACAO_SOBE, TQ_ACAO_DESCE,
    TQ_ACAO_COUNT
} tq_acao;

typedef enum { TQ_MODO_MENU = 0, TQ_MODO_PILOTAGEM = 1 } tq_modo;
typedef enum { TQ_DIRECAO_BOTOES = 0, TQ_DIRECAO_ANALOGICO = 1, TQ_DIRECAO_INCLINACAO = 2 } tq_direcao;

/* As telas, cada uma com as posições que o usuário deu aos botões: o Menu e a pilotagem de cada jeito de dirigir. */
typedef enum { TQ_TELA_MENU = 0, TQ_TELA_BOTOES, TQ_TELA_ANALOGICO, TQ_TELA_INCLINACAO, TQ_TELAS } tq_tela;

/* Os controles na tela. */
typedef enum {
    /* menu */
    TQ_C_DCIMA, TQ_C_DBAIXO, TQ_C_DESQ, TQ_C_DDIR,
    TQ_C_OK, TQ_C_VOLTAR, TQ_C_X, TQ_C_Y, TQ_C_LB, TQ_C_RB, TQ_C_START, TQ_C_BACK,
    /* pilotagem */
    TQ_C_ESQUERDA, TQ_C_DIREITA, TQ_C_ANALOGICO, TQ_C_RETO, TQ_C_DIRECAO,
    TQ_C_GAS, TQ_C_FREIO, TQ_C_NITRO, TQ_C_FREIO_MAO, TQ_C_SPEEDBREAKER, TQ_C_PARCEIRO,
    TQ_C_CAMERA, TQ_C_ENTRAR, TQ_C_PAUSA, TQ_C_RESET, TQ_C_MAPA, TQ_C_SMS, TQ_C_SOBE, TQ_C_DESCE,
    /* os dois modos */
    TQ_C_MODO, TQ_C_AJUSTES,
    TQ_C_COUNT
} tq_controle;

typedef enum { TQ_FORMA_CIRCULO = 0, TQ_FORMA_RETANGULO = 1 } tq_forma;

/* Os ícones: os desenhos do port do NFSU2 (Toque.m). */
typedef enum {
    TQ_ICONE_NENHUM = 0, TQ_ICONE_MENU, TQ_ICONE_PAUSA, TQ_ICONE_CAMERA, TQ_ICONE_RESET, TQ_ICONE_MAPA, TQ_ICONE_SMS,
    TQ_ICONE_ESQUERDA, TQ_ICONE_DIREITA, TQ_ICONE_CIMA, TQ_ICONE_BAIXO, /* triângulos cheios */
    TQ_ICONE_MARCHA_CIMA, TQ_ICONE_MARCHA_BAIXO,                       /* setas de traço */
    TQ_ICONE_BOTOES, TQ_ICONE_ANALOGICO, TQ_ICONE_INCLINACAO, TQ_ICONE_RETO,
} tq_icone;

typedef struct {
    int visivel;
    tq_forma forma;
    float cx, cy;         /* centro, em pontos da tela */
    float meia_l, meia_a; /* metade da largura e da altura (círculo: as duas são o raio) */
    float canto;          /* raio dos cantos */
    const char *rotulo;   /* UTF-8, "\n" separa as linhas; "" = só o ícone */
    int icone;            /* tq_icone */
    float letra;          /* tamanho da letra, em pontos */
    float tam_icone;      /* tamanho do ícone, em pontos */
    float traco;          /* espessura do traço do ícone, no desenho de 24 (NFSU2: 2; as marchas 2,5) */
    int borda;            /* o aro fino dos controles do Menu */
    int apertado;
} tq_info_controle;

/* A posição que o usuário deu a um controle numa tela: o centro deslocado (em pontos do tamanho 100%, então outro
 * tamanho geral mantém o arranjo) e o tamanho do controle (1 = o padrão). */
typedef struct {
    float dx, dy, tamanho;
} tq_posicao;
#define TQ_TAMANHO_MIN 0.5f
#define TQ_TAMANHO_MAX 2.5f

typedef struct {
    int direcao;                 /* tq_direcao */
    float escala;                /* tamanho de todos os controles: 0,8 a 1,5 (limitado ao que cabe na tela) */
    int cambio_manual;           /* mostra Marcha + e Marcha − */
    int pausa_vai_para_menu;     /* apertar Pausa pilotando troca para o modo Menu */
    uint8_t mapa[TQ_ACAO_COUNT]; /* tq_acao -> tq_saida */
} tq_ajustes;

/* O que vai para o controle virtual. Eixos na convenção do SDL: y positivo é para baixo. */
typedef struct {
    uint32_t botoes;      /* bit (1 << tq_saida) para TQ_O_A..TQ_O_DRIGHT */
    float lx, ly, rx, ry; /* -1..1 */
    float lt, rt;         /* 0..1 */
} tq_estado_controle;

/* Eventos para a parte de iOS (tq_eventos devolve e zera). */
enum {
    TQ_EV_TOQUE = 1u << 0,   /* um controle foi apertado: vibraçãozinha */
    TQ_EV_MODO = 1u << 1,    /* o modo mudou */
    TQ_EV_AJUSTES = 1u << 2, /* abrir os ajustes da camada */
    TQ_EV_RETO = 1u << 3,    /* recalibrar o "reto" da inclinação */
    TQ_EV_LAYOUT = 1u << 4,  /* os controles visíveis, as posições ou os tamanhos mudaram */
    TQ_EV_DIRECAO = 1u << 5, /* o botão de direção trocou o jeito de dirigir: salvar e avisar */
};

#define TQ_MAX_DEDOS 12

typedef struct {
    uintptr_t id;   /* 0 = livre */
    int controle;   /* tq_controle, ou -1 */
    float x0, y0, x, y; /* onde começou e onde está */
    double inicio;
    int atual;      /* pedais e setas: em qual o dedo está agora */
} tq_dedo;

typedef struct {
    tq_ajustes aj;
    int modo; /* tq_modo */
    float w, h;          /* tela, em pontos */
    float s;             /* escala em uso: o tamanho escolhido, limitado ao que cabe */
    float se, st, sd, sb; /* margens seguras: esquerda, topo, direita, baixo */
    tq_info_controle c[TQ_C_COUNT];
    float padrao_cx[TQ_C_COUNT], padrao_cy[TQ_C_COUNT]; /* o centro sem a posição do usuário */
    tq_posicao pos[TQ_TELAS][TQ_C_COUNT];
    tq_dedo dedos[TQ_MAX_DEDOS];
    double segurar_ate[TQ_O_COUNT];
    float volante;           /* direção atual (com rampa nos botões, suavizada na inclinação) */
    float inclinacao_graus;  /* inclinação já relativa ao "reto" */
    double ultimo_tick;
    int tick_inicial;
    /* analógico: a base (onde o polegar pousou; parada, no centro do controle), o polegar, a área e o fim de curso */
    float base_x, base_y, knob_x, knob_y;
    float zona_x0, zona_y0, zona_x1, zona_y1;
    float trava;
    int analogico_ativo;
    /* modo de mover */
    int editando, selecionado;
    tq_posicao ed_pos0[TQ_C_COUNT];             /* a posição do controle quando o gesto começou */
    float ed_x0[TQ_C_COUNT], ed_y0[TQ_C_COUNT]; /* o dedo (ou o meio da pinça) quando o gesto começou */
    float ed_d0[TQ_C_COUNT];                    /* a distância da pinça quando ela começou (0: um dedo só) */
    uint32_t eventos;
} tq_nucleo;

void tq_ajustes_padrao(tq_ajustes *aj);
void tq_iniciar(tq_nucleo *n, const tq_ajustes *aj);
void tq_definir_ajustes(tq_nucleo *n, const tq_ajustes *aj);
void tq_definir_modo(tq_nucleo *n, int modo);
/* A tela de agora (tq_tela): o modo e o jeito de dirigir. */
int tq_tela_atual(const tq_nucleo *n);
/* Tela em pontos e margens seguras. Recalcula as posições. */
void tq_layout(tq_nucleo *n, float w, float h, float esquerda, float topo, float direita, float baixo);
/* Qual controle está nesse ponto (-1: nenhum, o toque vai para o jogo). No modo de mover, o analógico é a base. */
int tq_controle_em(const tq_nucleo *n, float x, float y);
void tq_toque_comecou(tq_nucleo *n, uintptr_t id, float x, float y, double t);
void tq_toque_moveu(tq_nucleo *n, uintptr_t id, float x, float y, double t);
void tq_toque_acabou(tq_nucleo *n, uintptr_t id, double t);
/* Solta tudo (camada escondida, app em segundo plano). */
void tq_soltar_tudo(tq_nucleo *n);
/* Inclinação: ângulo da gravidade no plano da tela, em graus (atan2(g.y, g.x)), e o ângulo do "reto". */
float tq_inclinacao_relativa(float angulo, float reto);
void tq_definir_inclinacao(tq_nucleo *n, float graus_relativos);
/* Aperta saídas por um tempo (ex.: Back + Start juntos para os ajustes do SpeedBreaker). */
void tq_pulso(tq_nucleo *n, uint32_t saidas, double t, double duracao);
void tq_tick(tq_nucleo *n, double t, tq_estado_controle *saida);
uint32_t tq_eventos(tq_nucleo *n);

/* Modo de mover: um dedo arrasta o controle, dois dedos nele mudam o tamanho. Nada vai para o jogo. */
void tq_editar(tq_nucleo *n, int ligar);
tq_posicao tq_posicao_de(const tq_nucleo *n, int tela, int controle);
void tq_definir_posicao(tq_nucleo *n, int tela, int controle, tq_posicao p);
/* Volta os controles da tela ao lugar e ao tamanho padrão. */
void tq_restaurar_tela(tq_nucleo *n, int tela);
/* Mostra uma tela (para mover os botões de cada uma): o modo e o jeito de dirigir dela; solta tudo. */
void tq_ir_para_tela(tq_nucleo *n, int tela);

/* Nomes estáveis (para guardar as posições) e nomes para mostrar. */
const char *tq_id_controle(int controle);
int tq_controle_por_id(const char *id);
const char *tq_id_tela(int tela);
int tq_tela_por_id(const char *id);
const char *tq_nome_controle(int controle);
const char *tq_nome_tela(int tela);
const char *tq_nome_saida(int saida);
const char *tq_nome_acao(int acao);

#ifdef __cplusplus
}
#endif
#endif
