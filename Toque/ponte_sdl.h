/*
 * Toque: ponte com o SDL3 e o runtime do SpeedBreaker que estão dentro do executável do app.
 * Nada é linkado: as funções são achadas pelo nome (dlsym), entre as que o executável oficial exporta.
 * Testado em testes/testar_ponte.c, com um SDL de mentira.
 */
#ifndef TOQUE_PONTE_SDL_H
#define TOQUE_PONTE_SDL_H

#include <stddef.h>
#include <stdint.h>

#include "nucleo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Acha as funções. Devolve 1 se achou todas; senão a camada não liga. */
int ps_carregar(void);
int ps_funcoes_achadas(void);
int ps_funcoes_total(void);
/* Nomes das que faltaram, separados por espaço ("" se nenhuma). */
const char *ps_faltando(void);

/* O runtime já ligou o SDL dos controles? (SDL_WasInit(SDL_INIT_GAMEPAD)) */
int ps_sdl_pronto(void);
/* Cria o controle virtual e manda o estado neutro. Devolve 1 se conseguiu. */
int ps_conectar(void);
void ps_desconectar(void);
int ps_conectado(void);
uint32_t ps_id(void);
/* O jogo abriu o controle virtual como controle (SDL_GetGamepadFromID)? */
int ps_aberto_pelo_jogo(void);
/* Quantos controles o SDL vê além do virtual (um controle de verdade, por Bluetooth ou cabo). */
int ps_outros_controles(void);
/* Manda o estado; só o que mudou desde o último envio. */
void ps_enviar(const tq_estado_controle *s);
/* Último rumble que o jogo pediu ao controle virtual (0..65535 cada), e quantos pedidos já vieram. */
void ps_rumble(uint16_t *grave, uint16_t *agudo, uint32_t *pedidos);

/* Telas do SpeedBreaker: os ajustes abertos (ui::SettingsOpen) e uma tela cheia na frente do jogo, como o
 * instalador ou um aviso (ui::HasModal). Só da thread principal. */
int ps_ajustes_abertos(void);
int ps_tela_cheia(void);

/* Texto curto do estado, para o Diagnóstico. */
void ps_status_texto(char *buf, size_t n);

#ifdef __cplusplus
}
#endif
#endif
