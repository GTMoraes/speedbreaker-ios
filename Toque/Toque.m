/*
 * Toque: a parte de iOS dos controles por toque do NFS Carbon (SpeedBreaker).
 *
 * Entra no app como um framework que o executável carrega (scripts/injetar.py põe um LC_LOAD_WEAK_DYLIB).
 * Não mexe no jogo: cria um controle de Xbox 360 virtual no SDL do próprio app (ponte_sdl.c) e desenha os
 * controles numa janela transparente por cima. Toque fora de um controle passa direto para o jogo.
 *
 * O que decide o quê (posições, toques, modos, mover os botões) está no núcleo em C (nucleo.c, testado em
 * testes/). Aqui fica só o que é de iOS: janela, desenho (o do port do NFSU2: preto a 40%, texto branco em itálico
 * negrito, verde-limão quando apertado), toques, vibração, inclinação, ajustes, posições salvas e diagnóstico.
 *
 * Tudo roda na thread principal. O jogo prende a thread principal no laço dele e só deixa o iOS trabalhar
 * dentro do SDL_PollEvent, uma vez por quadro: é aí que os toques e o CADisplayLink daqui rodam.
 */
#import <CoreHaptics/CoreHaptics.h>
#import <CoreMotion/CoreMotion.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#include <math.h>
#include <os/proc.h>
#include <string.h>

#include "nucleo.h"
#include "ponte_sdl.h"

#ifndef TQ_BUILD
#define TQ_BUILD "local"
#endif

#define PI_ 3.14159265358979323846

typedef NS_ENUM(NSInteger, TQVisivel) {
    TQVisivelNada = 0,       /* janela escondida: ajustes do SpeedBreaker, ou o jogo mostrando algo do iOS */
    TQVisivelEngrenagem = 1, /* só o ≡: telas cheias do SpeedBreaker, controle de verdade, falta algo */
    TQVisivelTudo = 2,
};

typedef NS_ENUM(NSInteger, TQVibracao) {
    TQVibracaoTudo = 0,   /* toques e o rumble do jogo */
    TQVibracaoToques = 1, /* só nos toques */
    TQVibracaoNada = 2,
};

static const float ESCALAS[] = { 0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f }; /* as do NFSU2 */
static const float OPACIDADES[] = { 0.4f, 0.55f, 0.7f, 0.85f, 1.0f };
#define N_ESCALAS ((int)(sizeof ESCALAS / sizeof ESCALAS[0]))
#define N_OPACIDADES ((int)(sizeof OPACIDADES / sizeof OPACIDADES[0]))

static NSString *const K_DIRECAO = @"toque.direcao";
static NSString *const K_ESCALA = @"toque.escala";
static NSString *const K_OPACIDADE = @"toque.opacidade";
static NSString *const K_TRANSPARENCIA_ANTIGA = @"toque.transparencia"; /* a da entrega 2 (padrão 70%): não vale mais */
static NSString *const K_VIBRACAO = @"toque.vibracao";
static NSString *const K_CAMBIO = @"toque.cambio_manual";
static NSString *const K_PAUSA = @"toque.pausa_vai_para_menu";
static NSString *const K_MAPA_ANTIGO = @"toque.mapa"; /* até a entrega 2: o mapa inteiro, padrões incluídos */
static NSString *const K_TROCAS = @"toque.trocas";    /* só o que você reatribuiu: 0xFF = o padrão daquela ação */
static NSString *const K_POSICOES = @"toque.posicoes"; /* "tela.controle" -> [dx, dy, tamanho] */

static NSString *texto(const char *s) { return s ? ([NSString stringWithUTF8String:s] ?: @"") : @""; }

static NSString *nomeDirecao(int d)
{
    switch (d) {
    case TQ_DIRECAO_ANALOGICO: return @"Analógico";
    case TQ_DIRECAO_INCLINACAO: return @"Inclinar o iPhone";
    default: return @"Botões ◀ ▶";
    }
}

static NSString *nomeVibracao(NSInteger v)
{
    switch (v) {
    case TQVibracaoToques: return @"Só nos toques";
    case TQVibracaoNada: return @"Desligada";
    default: return @"Toques e jogo";
    }
}

static NSString *porcento(float f) { return [NSString stringWithFormat:@"%d%%", (int)lroundf(f * 100)]; }

/* O mapa padrão da entrega 2 (antes de o Mapa e o SMS irem para o direcional ← e →). Para migrar o "toque.mapa"
 * antigo, que guardava tudo: o que estiver igual a ele não foi escolha sua, e passa a seguir o padrão novo. */
static const uint8_t MAPA_PADRAO_ENTREGA2[TQ_ACAO_COUNT] = {
    [TQ_ACAO_FREIO_MAO] = TQ_O_A, [TQ_ACAO_NITRO] = TQ_O_B, [TQ_ACAO_SPEEDBREAKER] = TQ_O_X, [TQ_ACAO_PARCEIRO] = TQ_O_Y,
    [TQ_ACAO_RESET] = TQ_O_LB, [TQ_ACAO_CAMERA] = TQ_O_RB, [TQ_ACAO_OLHAR_TRAS] = TQ_O_RS, [TQ_ACAO_PAUSA] = TQ_O_START,
    [TQ_ACAO_ENTRAR] = TQ_O_BACK, [TQ_ACAO_MAPA] = TQ_O_DUP, [TQ_ACAO_SMS] = TQ_O_DDOWN, [TQ_ACAO_SOBE] = TQ_O_RUP,
    [TQ_ACAO_DESCE] = TQ_O_RDOWN,
};
#define TQ_PADRAO 0xFF

/* ---------- as cores e as letras do NFSU2 (web/src/app/style.css) ---------- */

static UIColor *corLima(void) { return [UIColor colorWithRed:200 / 255.0 green:240 / 255.0 blue:136 / 255.0 alpha:1]; }
static UIColor *corLimaFundo(void) { return [UIColor colorWithRed:64 / 255.0 green:64 / 255.0 blue:56 / 255.0 alpha:1]; }
static UIColor *corTexto(void) { return [UIColor colorWithWhite:1 alpha:0.72]; }
static UIColor *corFundo(BOOL apertado) { return [UIColor colorWithWhite:0 alpha:apertado ? 0.72 : 0.4]; }

static UIFont *fonteNegritoItalico(CGFloat tam)
{
    return [UIFont fontWithName:@"HelveticaNeue-BoldItalic" size:tam] ?: [UIFont italicSystemFontOfSize:tam];
}

static UIFont *fonteNormal(CGFloat tam) { return [UIFont fontWithName:@"HelveticaNeue" size:tam] ?: [UIFont systemFontOfSize:tam]; }

/* ---------- desenhos ---------- */

/* Retângulo com cantos de arco de círculo (como o border-radius do CSS; o do UIKit tem outra curva). */
static UIBezierPath *retanguloArredondado(CGRect q, CGFloat r)
{
    const CGFloat x = q.origin.x, y = q.origin.y, w = q.size.width, h = q.size.height;
    r = MAX(0, MIN(r, MIN(w, h) / 2));
    UIBezierPath *b = [UIBezierPath bezierPath];
    [b moveToPoint:CGPointMake(x + r, y)];
    [b addLineToPoint:CGPointMake(x + w - r, y)];
    [b addArcWithCenter:CGPointMake(x + w - r, y + r) radius:r startAngle:-PI_ / 2 endAngle:0 clockwise:YES];
    [b addLineToPoint:CGPointMake(x + w, y + h - r)];
    [b addArcWithCenter:CGPointMake(x + w - r, y + h - r) radius:r startAngle:0 endAngle:PI_ / 2 clockwise:YES];
    [b addLineToPoint:CGPointMake(x + r, y + h)];
    [b addArcWithCenter:CGPointMake(x + r, y + h - r) radius:r startAngle:PI_ / 2 endAngle:PI_ clockwise:YES];
    [b addLineToPoint:CGPointMake(x, y + r)];
    [b addArcWithCenter:CGPointMake(x + r, y + r) radius:r startAngle:PI_ endAngle:PI_ * 1.5 clockwise:YES];
    [b closePath];
    return b;
}

/* A forma do controle num retângulo (o círculo, ou o retângulo com os cantos dele crescidos de "mais"). */
static UIBezierPath *caminhoForma(const tq_info_controle *k, CGRect q, CGFloat mais)
{
    if (k->forma == TQ_FORMA_CIRCULO)
        return [UIBezierPath bezierPathWithOvalInRect:q];
    return retanguloArredondado(q, k->canto + mais);
}

static void linha(UIBezierPath *b, CGFloat x0, CGFloat y0, CGFloat x1, CGFloat y1)
{
    [b moveToPoint:CGPointMake(x0, y0)];
    [b addLineToPoint:CGPointMake(x1, y1)];
}

/* Um polígono: n pontos (x, y) e fechado ou não. */
static void poligono(UIBezierPath *b, const CGFloat *p, int n, BOOL fechar)
{
    [b moveToPoint:CGPointMake(p[0], p[1])];
    for (int i = 1; i < n; i++)
        [b addLineToPoint:CGPointMake(p[2 * i], p[2 * i + 1])];
    if (fechar)
        [b closePath];
}

/* Os ícones do NFSU2 (web/src/app/touch.ts, ICONS), no desenho de 24 x 24: o que é traço e o que é cheio. */
static void desenharIcone(int icone, UIBezierPath *traco, UIBezierPath *cheio)
{
    switch (icone) {
    case TQ_ICONE_MENU: /* M5 7h14M5 12h14M5 17h14 */
        linha(traco, 5, 7, 19, 7);
        linha(traco, 5, 12, 19, 12);
        linha(traco, 5, 17, 19, 17);
        break;
    case TQ_ICONE_PAUSA: /* M9 6v12M15 6v12 */
        linha(traco, 9, 6, 9, 18);
        linha(traco, 15, 6, 15, 18);
        break;
    case TQ_ICONE_CAMERA: { /* M4 8h3l2-2h6l2 2h3v10H4z + círculo (12,13) r3 */
        const CGFloat p[] = { 4, 8, 7, 8, 9, 6, 15, 6, 17, 8, 20, 8, 20, 18, 4, 18 };
        poligono(traco, p, 8, YES);
        [traco appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(9, 10, 6, 6)]];
        break;
    }
    case TQ_ICONE_RESET: /* M6 13a6 6 0 1 0 2-5.2 (o arco grande, anti-horário) + M5 4v4h4 */
        [traco moveToPoint:CGPointMake(6, 13)];
        [traco addArcWithCenter:CGPointMake(11.9599, 12.3077) radius:6 startAngle:3.0259 endAngle:-2.2916 clockwise:NO];
        {
            const CGFloat p[] = { 5, 4, 5, 8, 9, 8 };
            poligono(traco, p, 3, NO);
        }
        break;
    case TQ_ICONE_MAPA: { /* M4 6l5-2 6 2 5-2v14l-5 2-6-2-5 2z + M9 4v14M15 6v14 */
        const CGFloat p[] = { 4, 6, 9, 4, 15, 6, 20, 4, 20, 18, 15, 20, 9, 18, 4, 20 };
        poligono(traco, p, 8, YES);
        linha(traco, 9, 4, 9, 18);
        linha(traco, 15, 6, 15, 20);
        break;
    }
    case TQ_ICONE_SMS: { /* M4 6h16v12H4z + M4 7l8 6 8-6 */
        const CGFloat p[] = { 4, 6, 20, 6, 20, 18, 4, 18 };
        poligono(traco, p, 4, YES);
        const CGFloat q[] = { 4, 7, 12, 13, 20, 7 };
        poligono(traco, q, 3, NO);
        break;
    }
    case TQ_ICONE_ESQUERDA: { /* M16 5L7 12l9 7z */
        const CGFloat p[] = { 16, 5, 7, 12, 16, 19 };
        poligono(cheio, p, 3, YES);
        break;
    }
    case TQ_ICONE_DIREITA: { /* M8 5l9 7-9 7z */
        const CGFloat p[] = { 8, 5, 17, 12, 8, 19 };
        poligono(cheio, p, 3, YES);
        break;
    }
    case TQ_ICONE_CIMA: { /* M5 16l7-9 7 9z */
        const CGFloat p[] = { 5, 16, 12, 7, 19, 16 };
        poligono(cheio, p, 3, YES);
        break;
    }
    case TQ_ICONE_BAIXO: { /* M5 8l7 9 7-9z */
        const CGFloat p[] = { 5, 8, 12, 17, 19, 8 };
        poligono(cheio, p, 3, YES);
        break;
    }
    case TQ_ICONE_MARCHA_CIMA: { /* M6 15l6-6 6 6 */
        const CGFloat p[] = { 6, 15, 12, 9, 18, 15 };
        poligono(traco, p, 3, NO);
        break;
    }
    case TQ_ICONE_MARCHA_BAIXO: { /* M6 9l6 6 6-6 */
        const CGFloat p[] = { 6, 9, 12, 15, 18, 9 };
        poligono(traco, p, 3, NO);
        break;
    }
    case TQ_ICONE_BOTOES: { /* M10 7l-5 5 5 5M14 7l5 5-5 5 */
        const CGFloat p[] = { 10, 7, 5, 12, 10, 17 }, q[] = { 14, 7, 19, 12, 14, 17 };
        poligono(traco, p, 3, NO);
        poligono(traco, q, 3, NO);
        break;
    }
    case TQ_ICONE_ANALOGICO: /* círculo r8 + bolinha cheia r3 */
        [traco appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(4, 4, 16, 16)]];
        [cheio appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(9, 9, 6, 6)]];
        break;
    case TQ_ICONE_INCLINACAO: { /* rect x4 y8 w16 h9 rx2, girado -18 graus em volta de (12, 12) */
        UIBezierPath *r = retanguloArredondado(CGRectMake(4, 8, 16, 9), 2);
        CGAffineTransform t = CGAffineTransformMakeTranslation(12, 12);
        t = CGAffineTransformRotate(t, -18 * PI_ / 180);
        t = CGAffineTransformTranslate(t, -12, -12);
        [r applyTransform:t];
        [traco appendPath:r];
        break;
    }
    case TQ_ICONE_RETO: /* círculo r6 + M12 3v3M12 18v3M3 12h3M18 12h3 */
        [traco appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(6, 6, 12, 12)]];
        linha(traco, 12, 3, 12, 6);
        linha(traco, 12, 18, 12, 21);
        linha(traco, 3, 12, 6, 12);
        linha(traco, 18, 12, 21, 12);
        break;
    default:
        break;
    }
}

@class TQCamada;

/* ---------- uma peça desenhada (um controle) ---------- */

@interface TQPeca : UIView
- (instancetype)initWithControle:(int)c;
- (void)posicionar:(const tq_info_controle *)k;
- (void)mostrarApertado:(BOOL)apertado;
- (void)mostrarContorno:(int)tipo; /* 0: nenhum; 1: tracejado (mover os botões); 2: o escolhido */
@end

@implementation TQPeca {
    int _c;
    CAShapeLayer *_fundo, *_aro, *_traco, *_cheio, *_contorno;
    UILabel *_rotulo;
    int _apertado, _tipoContorno;
    tq_info_controle _atual;
    BOOL _desenhada;
}

- (instancetype)initWithControle:(int)c
{
    if ((self = [super initWithFrame:CGRectZero])) {
        _c = c;
        _apertado = -1;
        _tipoContorno = -1;
        self.userInteractionEnabled = NO; /* quem recebe os toques é a TQVista */
        self.backgroundColor = UIColor.clearColor;
        _fundo = [CAShapeLayer layer];
        [self.layer addSublayer:_fundo];
        _aro = [CAShapeLayer layer]; /* NFSU2 .tc.menu: inset 0 0 0 1.5px rgba(255, 255, 255, 0.2) */
        _aro.fillColor = nil;
        _aro.strokeColor = [UIColor colorWithWhite:1 alpha:0.2].CGColor;
        _aro.lineWidth = 1.5;
        [self.layer addSublayer:_aro];
        _traco = [CAShapeLayer layer];
        _traco.fillColor = nil;
        _traco.lineCap = kCALineCapRound;
        _traco.lineJoin = kCALineJoinRound;
        [self.layer addSublayer:_traco];
        _cheio = [CAShapeLayer layer];
        [self.layer addSublayer:_cheio];
        _rotulo = [[UILabel alloc] initWithFrame:CGRectZero];
        _rotulo.textAlignment = NSTextAlignmentCenter;
        _rotulo.lineBreakMode = NSLineBreakByClipping;
        [self addSubview:_rotulo];
        _contorno = [CAShapeLayer layer];
        _contorno.fillColor = nil;
        _contorno.hidden = YES;
        [self.layer addSublayer:_contorno];
    }
    return self;
}

static BOOL mesmoDesenho(const tq_info_controle *a, const tq_info_controle *b)
{
    return a->forma == b->forma && a->cx == b->cx && a->cy == b->cy && a->meia_l == b->meia_l &&
           a->meia_a == b->meia_a && a->canto == b->canto && a->rotulo == b->rotulo && a->icone == b->icone &&
           a->letra == b->letra && a->tam_icone == b->tam_icone && a->traco == b->traco && a->borda == b->borda;
}

- (void)posicionar:(const tq_info_controle *)k
{
    if (_desenhada && mesmoDesenho(k, &_atual))
        return;
    _desenhada = YES;
    _atual = *k;
    const CGFloat w = 2 * k->meia_l, h = 2 * k->meia_a;
    self.frame = CGRectMake(k->cx - k->meia_l, k->cy - k->meia_a, w, h);
    const CGRect b = CGRectMake(0, 0, w, h);
    for (CAShapeLayer *l in @[ _fundo, _aro, _traco, _cheio, _contorno ])
        l.frame = b;
    _fundo.path = caminhoForma(k, b, 0).CGPath;
    _aro.hidden = !k->borda;
    _aro.path = caminhoForma(k, CGRectInset(b, 0.75, 0.75), -0.75).CGPath;
    _contorno.path = caminhoForma(k, CGRectInset(b, -3, -3), 3).CGPath;

    UIBezierPath *traco = [UIBezierPath bezierPath], *cheio = [UIBezierPath bezierPath];
    desenharIcone(k->icone, traco, cheio);
    const CGFloat tam = k->tam_icone;
    CGAffineTransform t = CGAffineTransformMakeTranslation(w / 2 - tam / 2, h / 2 - tam / 2);
    t = CGAffineTransformScale(t, tam / 24, tam / 24);
    [traco applyTransform:t];
    [cheio applyTransform:t];
    _traco.path = traco.CGPath;
    _traco.lineWidth = k->traco * tam / 24; /* o stroke-width do SVG é no desenho de 24 */
    _cheio.path = cheio.CGPath;
    [self ajustarRotulo:k];
    _apertado = -1;
    [self mostrarApertado:k->apertado != 0];
}

/* O rótulo nunca quebra palavra: cada linha do texto é uma linha na tela. Como o wordPx do NFSU2: a letra do
 * controle, ou menor até a linha mais larga caber (num redondo, em 40 de cada 48 da largura), 75% dela no mínimo. */
- (void)ajustarRotulo:(const tq_info_controle *)k
{
    NSString *t = texto(k->rotulo);
    _rotulo.hidden = t.length == 0;
    if (!t.length)
        return;
    NSArray<NSString *> *linhas = [t componentsSeparatedByString:@"\n"];
    const BOOL circulo = k->forma == TQ_FORMA_CIRCULO;
    const CGFloat largura = circulo ? 2 * k->meia_l * 40 / 48 : 2 * k->meia_l - k->letra / 2;
    CGFloat tam = k->letra;
    UIFont *fonte = fonteNegritoItalico(tam);
    CGFloat maisLarga = 0;
    for (NSString *l in linhas)
        maisLarga = MAX(maisLarga, [l sizeWithAttributes:@{ NSFontAttributeName : fonte }].width);
    if (maisLarga > largura && maisLarga > 0) {
        tam = MAX(floor(tam * largura / maisLarga * 2) / 2, MAX(k->letra * 0.75, 6));
        fonte = fonteNegritoItalico(tam);
    }
    const CGFloat altura = fonte.lineHeight * (CGFloat)linhas.count;
    if (altura > 2 * k->meia_a && altura > 0) {
        tam = MAX(floor(tam * 2 * k->meia_a / altura * 2) / 2, 6);
        fonte = fonteNegritoItalico(tam);
    }
    _rotulo.frame = CGRectMake(0, 0, 2 * k->meia_l, 2 * k->meia_a);
    _rotulo.numberOfLines = (NSInteger)linhas.count;
    _rotulo.font = fonte;
    _rotulo.text = t;
}

- (void)mostrarApertado:(BOOL)apertado
{
    if ((int)apertado == _apertado)
        return;
    _apertado = apertado;
    /* NFSU2 .tc.on: o fundo mais escuro e o texto verde-limão */
    UIColor *cor = apertado ? corLima() : corTexto();
    _fundo.fillColor = corFundo(apertado).CGColor;
    _traco.strokeColor = cor.CGColor;
    _cheio.fillColor = cor.CGColor;
    _rotulo.textColor = cor;
}

- (void)mostrarContorno:(int)tipo
{
    if (tipo == _tipoContorno)
        return;
    _tipoContorno = tipo;
    _contorno.hidden = tipo == 0;
    if (tipo == 2) {
        _contorno.strokeColor = corLima().CGColor;
        _contorno.lineWidth = 2.5;
        _contorno.lineDashPattern = nil;
    } else {
        _contorno.strokeColor = [UIColor colorWithWhite:1 alpha:0.6].CGColor;
        _contorno.lineWidth = 1.5;
        _contorno.lineDashPattern = @[ @5, @4 ];
    }
}

@end

/* ---------- o painel do modo de mover ---------- */

@interface TQPainel : UIView
@property (nonatomic, copy) void (^aoEscolherTela)(int tela);
@property (nonatomic, copy) void (^aoTocar)(int botao); /* 0: Cancelar, 1: Padrão, 2: Pronto */
- (void)mostrarTela:(int)tela;
- (void)mostrarControle:(int)c tamanho:(float)tamanho;
@end

@implementation TQPainel {
    UILabel *_titulo, *_info, *_dica;
    UISegmentedControl *_telas;
    UIButton *__strong _botoes[3];
    int _controleMostrado;
    float _tamanhoMostrado;
}

- (UILabel *)rotulo:(NSString *)t fonte:(UIFont *)f cor:(UIColor *)c
{
    UILabel *l = [[UILabel alloc] initWithFrame:CGRectZero];
    l.text = t;
    l.font = f;
    l.textColor = c;
    l.textAlignment = NSTextAlignmentCenter;
    l.adjustsFontSizeToFitWidth = YES;
    l.minimumScaleFactor = 0.7;
    [self addSubview:l];
    return l;
}

- (instancetype)initWithFrame:(CGRect)quadro
{
    if ((self = [super initWithFrame:quadro])) {
        /* NFSU2: a moldura escura dos diálogos, títulos em verde-limão, botões pretos de aro cinza */
        self.backgroundColor = [UIColor colorWithRed:23 / 255.0 green:23 / 255.0 blue:23 / 255.0 alpha:0.94];
        self.layer.cornerRadius = 12;
        self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
        _titulo = [self rotulo:@"Mover os botões" fonte:fonteNegritoItalico(17) cor:corLima()];
        _telas = [[UISegmentedControl alloc] initWithItems:@[ @"Menu", @"Botões", @"Analógico", @"Inclinação" ]];
        _telas.selectedSegmentTintColor = corLimaFundo();
        [_telas setTitleTextAttributes:@{ NSFontAttributeName : fonteNegritoItalico(13),
                                          NSForegroundColorAttributeName : corTexto() }
                              forState:UIControlStateNormal];
        [_telas setTitleTextAttributes:@{ NSFontAttributeName : fonteNegritoItalico(13),
                                          NSForegroundColorAttributeName : corLima() }
                              forState:UIControlStateSelected];
        [_telas addTarget:self action:@selector(trocouTela:) forControlEvents:UIControlEventValueChanged];
        [self addSubview:_telas];
        _info = [self rotulo:@"" fonte:fonteNegritoItalico(15) cor:UIColor.whiteColor];
        _controleMostrado = -2;
        _dica = [self rotulo:@"Arraste para mover. Dois dedos para mudar o tamanho."
                       fonte:fonteNormal(12)
                         cor:[UIColor colorWithWhite:1 alpha:0.6]];
        NSString *const nomes[3] = { @"Cancelar", @"Padrão", @"Pronto" };
        for (int i = 0; i < 3; i++) {
            UIButton *b = [UIButton buttonWithType:UIButtonTypeCustom];
            const BOOL pronto = i == 2;
            [b setTitle:nomes[i] forState:UIControlStateNormal];
            [b setTitleColor:pronto ? corLima() : UIColor.whiteColor forState:UIControlStateNormal];
            [b setTitleColor:corLima() forState:UIControlStateHighlighted];
            b.titleLabel.font = fonteNegritoItalico(15);
            b.backgroundColor = pronto ? corLimaFundo() : [UIColor colorWithRed:8 / 255.0 green:8 / 255.0 blue:8 / 255.0 alpha:1];
            b.layer.cornerRadius = 8;
            b.layer.borderWidth = 2;
            b.layer.borderColor = (pronto ? corLima() : [UIColor colorWithRed:138 / 255.0 green:138 / 255.0 blue:138 / 255.0 alpha:1]).CGColor;
            b.tag = i;
            [b addTarget:self action:@selector(tocouBotao:) forControlEvents:UIControlEventTouchUpInside];
            [self addSubview:b];
            _botoes[i] = b;
        }
    }
    return self;
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    const CGFloat w = self.bounds.size.width;
    _titulo.frame = CGRectMake(16, 12, w - 32, 22);
    _telas.frame = CGRectMake(12, 42, w - 24, 32);
    _info.frame = CGRectMake(16, 84, w - 32, 20);
    _dica.frame = CGRectMake(16, 104, w - 32, 18);
    const CGFloat bw = (w - 24 - 16) / 3;
    for (int i = 0; i < 3; i++)
        _botoes[i].frame = CGRectMake(12 + i * (bw + 8), 132, bw, 40);
}

+ (CGSize)tamanhoPara:(CGSize)tela { return CGSizeMake(MIN(360, tela.width - 32), 184); }

- (void)trocouTela:(UISegmentedControl *)s
{
    if (self.aoEscolherTela)
        self.aoEscolherTela((int)s.selectedSegmentIndex);
}

- (void)tocouBotao:(UIButton *)b
{
    if (self.aoTocar)
        self.aoTocar((int)b.tag);
}

- (void)mostrarTela:(int)tela
{
    if (_telas.selectedSegmentIndex != tela)
        _telas.selectedSegmentIndex = tela;
}

- (void)mostrarControle:(int)c tamanho:(float)tamanho
{
    if (c == _controleMostrado && tamanho == _tamanhoMostrado)
        return;
    _controleMostrado = c;
    _tamanhoMostrado = tamanho;
    _info.text = c < 0 ? @"Toque num botão para escolher"
                       : [NSString stringWithFormat:@"%@ · %@", texto(tq_nome_controle(c)), porcento(tamanho)];
}

@end

/* ---------- a vista com todos os controles ---------- */

@interface TQVista : UIView
@property (nonatomic, weak) TQCamada *camada;
@property (nonatomic, assign) tq_nucleo *nucleo;
@property (nonatomic) TQVisivel visivel;
@property (nonatomic) CGFloat opacidade;
@property (nonatomic) BOOL editando;
@property (nonatomic, readonly) TQPainel *painel;
- (void)refazer;
- (void)atualizarApertos;
- (void)atualizarPainel;
- (CGRect)quadroDaEngrenagem;
- (void)avisar:(NSString *)t por:(NSTimeInterval)segundos;
@end

@interface TQCamada : NSObject
+ (instancetype)compartilhada;
- (void)comecar;
- (void)layoutMudou;
- (BOOL)aceitaToqueEm:(CGPoint)p;
- (void)toques:(NSSet<UITouch *> *)toques fase:(int)fase;
- (UIViewController *)controladorDoJogo;
@end

@implementation TQVista {
    TQPeca *__strong _pecas[TQ_C_COUNT];
    CAShapeLayer *_base, *_bola, *_contornoBase; /* analógico (NFSU2 .stick-base e .stick-knob) */
    UIView *_aviso;
    UILabel *_avisoTexto;
    BOOL _painelSumido;
}

- (instancetype)initWithFrame:(CGRect)quadro
{
    if ((self = [super initWithFrame:quadro])) {
        self.backgroundColor = UIColor.clearColor;
        self.opaque = NO;
        self.multipleTouchEnabled = YES;
        _opacidade = 1;
        _base = [CAShapeLayer layer];
        _base.lineWidth = 2;
        _base.hidden = YES;
        [self.layer addSublayer:_base];
        _bola = [CAShapeLayer layer];
        _bola.lineWidth = 2;
        _bola.hidden = YES;
        [self.layer addSublayer:_bola];
        _contornoBase = [CAShapeLayer layer];
        _contornoBase.fillColor = nil;
        _contornoBase.hidden = YES;
        [self.layer addSublayer:_contornoBase];
        for (int c = 0; c < TQ_C_COUNT; c++) {
            if (c == TQ_C_ANALOGICO)
                continue;
            _pecas[c] = [[TQPeca alloc] initWithControle:c];
            _pecas[c].hidden = YES;
            [self addSubview:_pecas[c]];
        }
        _painel = [[TQPainel alloc] initWithFrame:CGRectZero];
        _painel.hidden = YES;
        [self addSubview:_painel];
        /* os avisos (NFSU2 .toasts): embaixo da fileira de cima */
        _aviso = [[UIView alloc] initWithFrame:CGRectZero];
        _aviso.backgroundColor = [UIColor colorWithWhite:0 alpha:0.8];
        _aviso.layer.cornerRadius = 10;
        _aviso.userInteractionEnabled = NO;
        _aviso.alpha = 0;
        _avisoTexto = [[UILabel alloc] initWithFrame:CGRectZero];
        _avisoTexto.font = fonteNormal(15);
        _avisoTexto.textColor = UIColor.whiteColor;
        _avisoTexto.textAlignment = NSTextAlignmentCenter;
        _avisoTexto.numberOfLines = 0;
        [_aviso addSubview:_avisoTexto];
        [self addSubview:_aviso];
    }
    return self;
}

- (BOOL)pointInside:(CGPoint)p withEvent:(UIEvent *)e
{
    (void)e;
    return self.editando || [self.camada aceitaToqueEm:p];
}

/* Movendo os botões, todo toque é daqui (nada vai para o jogo): os botões e a escolha de tela do painel ficam com
 * os toques deles; o resto, até o fundo do painel, arrasta o controle que estiver ali. */
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e
{
    if (![self pointInside:p withEvent:e])
        return nil;
    if (self.editando) {
        UIView *v = [super hitTest:p withEvent:e];
        for (UIView *x = v; x && x != self; x = x.superview)
            if ([x isKindOfClass:UIControl.class])
                return v;
    }
    return self;
}

- (void)touchesBegan:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { (void)e; [self.camada toques:t fase:0]; }
- (void)touchesMoved:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { (void)e; [self.camada toques:t fase:1]; }
- (void)touchesEnded:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { (void)e; [self.camada toques:t fase:2]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { (void)e; [self.camada toques:t fase:2]; }

- (void)refazer
{
    const tq_nucleo *n = self.nucleo;
    if (!n)
        return;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.backgroundColor = self.editando ? [UIColor colorWithWhite:0 alpha:0.3] : UIColor.clearColor;
    for (int c = 0; c < TQ_C_COUNT; c++) {
        TQPeca *p = _pecas[c];
        if (!p)
            continue;
        const tq_info_controle *k = &n->c[c];
        const BOOL mostra = k->visivel && (self.visivel == TQVisivelTudo ||
                                           (self.visivel == TQVisivelEngrenagem && c == TQ_C_AJUSTES));
        p.hidden = !mostra;
        if (mostra) {
            [p posicionar:k];
            p.alpha = self.editando ? 1 : (self.visivel == TQVisivelTudo ? self.opacidade : MIN(self.opacidade, 0.5));
        }
    }
    const tq_info_controle *a = &n->c[TQ_C_ANALOGICO];
    const BOOL analogico = self.visivel == TQVisivelTudo && a->visivel;
    _base.hidden = _bola.hidden = !analogico;
    if (analogico) {
        /* NFSU2: base de 120 com borda de 2 (dentro dela), bolinha de 52 */
        const CGFloat rb = a->meia_l, rk = a->meia_l * 52 / 120;
        _base.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(-rb + 1, -rb + 1, 2 * rb - 2, 2 * rb - 2)].CGPath;
        _bola.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(-rk + 1, -rk + 1, 2 * rk - 2, 2 * rk - 2)].CGPath;
        _contornoBase.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(-rb - 3, -rb - 3, 2 * rb + 6, 2 * rb + 6)].CGPath;
        _base.opacity = _bola.opacity = self.editando ? 1.0f : (float)self.opacidade;
    }
    [CATransaction commit];
    [self atualizarApertos];
    [self atualizarPainel];
}

- (void)atualizarApertos
{
    const tq_nucleo *n = self.nucleo;
    if (!n || self.visivel == TQVisivelNada)
        return;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (int c = 0; c < TQ_C_COUNT; c++) {
        TQPeca *p = _pecas[c];
        if (!p || p.hidden)
            continue;
        [p mostrarApertado:n->c[c].apertado != 0];
        [p mostrarContorno:!self.editando ? 0 : (c == n->selecionado ? 2 : 1)];
    }
    if (!_base.hidden) {
        const BOOL ativo = n->analogico_ativo != 0;
        _base.position = CGPointMake(n->base_x, n->base_y);
        _bola.position = CGPointMake(n->knob_x, n->knob_y);
        /* NFSU2 .stick-base.on: a borda em verde-limão */
        _base.fillColor = [UIColor colorWithWhite:0 alpha:0.24].CGColor;
        _base.strokeColor = (ativo ? [UIColor colorWithRed:200 / 255.0 green:240 / 255.0 blue:136 / 255.0 alpha:0.5]
                                   : [UIColor colorWithWhite:1 alpha:0.24]).CGColor;
        _bola.fillColor = corFundo(ativo).CGColor;
        _bola.strokeColor = (ativo ? corLima() : [UIColor colorWithWhite:1 alpha:0.5]).CGColor;
        _contornoBase.hidden = !self.editando;
        if (self.editando) {
            const BOOL escolhido = n->selecionado == TQ_C_ANALOGICO;
            _contornoBase.position = CGPointMake(n->c[TQ_C_ANALOGICO].cx, n->c[TQ_C_ANALOGICO].cy);
            _contornoBase.strokeColor = (escolhido ? corLima() : [UIColor colorWithWhite:1 alpha:0.6]).CGColor;
            _contornoBase.lineWidth = escolhido ? 2.5 : 1.5;
            _contornoBase.lineDashPattern = escolhido ? nil : @[ @5, @4 ];
        }
    } else {
        _contornoBase.hidden = YES;
    }
    [CATransaction commit];
}

/* Quantos dedos estão num controle agora (mover os botões: o painel some enquanto arrasta). */
static int dedosNosControles(const tq_nucleo *n)
{
    int k = 0;
    for (int i = 0; i < TQ_MAX_DEDOS; i++)
        if (n->dedos[i].controle >= 0)
            k++;
    return k;
}

/* O painel onde ele cobre menos controles: no meio, embaixo da fileira de cima, ou embaixo. */
- (void)posicionarPainel
{
    const tq_nucleo *n = self.nucleo;
    const CGRect b = self.bounds;
    const CGSize tam = [TQPainel tamanhoPara:b.size];
    const CGFloat x = (b.size.width - tam.width) / 2;
    const CGFloat t = MAX(12, self.safeAreaInsets.top), baixo = MAX(12, self.safeAreaInsets.bottom);
    const CGFloat ys[3] = { (b.size.height - tam.height) / 2, t + 56 * n->s,
                            b.size.height - baixo - tam.height };
    int melhor = 0;
    CGFloat menor = 1e18;
    for (int i = 0; i < 3; i++) {
        const CGRect q = CGRectMake(x, ys[i], tam.width, tam.height);
        CGFloat coberto = 0;
        for (int c = 0; c < TQ_C_COUNT; c++) {
            const tq_info_controle *k = &n->c[c];
            if (!k->visivel)
                continue;
            const CGRect r = CGRectIntersection(q, CGRectMake(k->cx - k->meia_l - 4, k->cy - k->meia_a - 4,
                                                              2 * k->meia_l + 8, 2 * k->meia_a + 8));
            if (!CGRectIsNull(r))
                coberto += r.size.width * r.size.height;
        }
        if (coberto < menor - 1) {
            menor = coberto;
            melhor = i;
        }
    }
    const CGRect q = CGRectMake(x, ys[melhor], tam.width, tam.height);
    if (!CGRectEqualToRect(q, _painel.frame))
        _painel.frame = q;
}

- (void)atualizarPainel
{
    const tq_nucleo *n = self.nucleo;
    if (!n)
        return;
    _painel.hidden = !self.editando;
    if (!self.editando)
        return;
    const int c = n->selecionado;
    [_painel mostrarControle:c tamanho:c >= 0 ? tq_posicao_de(n, tq_tela_atual(n), c).tamanho : 1];
    [_painel mostrarTela:tq_tela_atual(n)];
    const BOOL sumir = dedosNosControles(n) > 0;
    if (!sumir)
        [self posicionarPainel]; /* depois de cada arrasto e de cada troca de tela */
    if (sumir != _painelSumido) {
        _painelSumido = sumir;
        _painel.userInteractionEnabled = !sumir;
        [UIView animateWithDuration:0.15
                         animations:^{
                             self->_painel.alpha = sumir ? 0 : 1;
                         }];
    }
}

- (CGRect)quadroDaEngrenagem
{
    TQPeca *p = _pecas[TQ_C_AJUSTES];
    return p && !p.hidden ? p.frame : CGRectMake(10, 10, 50, 50);
}

- (void)avisar:(NSString *)t por:(NSTimeInterval)segundos
{
    const tq_nucleo *n = self.nucleo;
    const CGRect b = self.bounds;
    const CGFloat largura = MIN(520, b.size.width - 48);
    _avisoTexto.text = t;
    const CGSize cabe = [_avisoTexto sizeThatFits:CGSizeMake(largura - 24, 200)];
    const CGFloat w = MIN(largura, ceil(cabe.width) + 24), h = ceil(cabe.height) + 16;
    const CGFloat topo = MAX(12, self.safeAreaInsets.top) + 12 + 48 * (n ? n->s : 1);
    _aviso.frame = CGRectMake((b.size.width - w) / 2, topo, w, h);
    _avisoTexto.frame = CGRectMake(12, 8, w - 24, h - 16);
    /* um aviso novo tira o anterior: mostra já e some depois de "segundos" */
    [_aviso.layer removeAllAnimations];
    _aviso.alpha = 1;
    UIView *aviso = _aviso;
    [UIView animateWithDuration:0.3
                          delay:segundos
                        options:UIViewAnimationOptionAllowUserInteraction
                     animations:^{
                         aviso.alpha = 0;
                     }
                     completion:nil];
}

@end

/* ---------- a janela e o controlador dela ---------- */

@interface TQJanela : UIWindow
@end

@implementation TQJanela

/* Nunca a janela principal: o teclado e o resto continuam com o jogo. */
- (BOOL)canBecomeKeyWindow
{
    return NO;
}

/* Só pega o toque que a TQVista quer (um controle; movendo os botões, tudo) ou, com uma folha aberta, tudo. */
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e
{
    UIView *v = [super hitTest:p withEvent:e];
    if (!v || v == self)
        return nil;
    UIView *raiz = self.rootViewController.view;
    if (v == raiz || [v isDescendantOfView:raiz] || self.rootViewController.presentedViewController)
        return v;
    return nil;
}

@end

@interface TQControlador : UIViewController
@property (nonatomic, weak) TQCamada *camada;
@end

@implementation TQControlador

- (void)loadView
{
    TQVista *v = [[TQVista alloc] initWithFrame:CGRectZero];
    v.camada = self.camada;
    self.view = v;
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    [self.camada layoutMudou];
}

- (void)viewSafeAreaInsetsDidChange
{
    [super viewSafeAreaInsetsDidChange];
    [self.camada layoutMudou];
}

/* A barra de status, o indicador de início, as bordas e a rotação seguem o que o jogo pede: esta janela fica
 * por cima e o iOS pergunta para ela. */
- (BOOL)prefersStatusBarHidden
{
    UIViewController *j = [self.camada controladorDoJogo];
    return j ? j.prefersStatusBarHidden : YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden
{
    UIViewController *j = [self.camada controladorDoJogo];
    return j ? j.prefersHomeIndicatorAutoHidden : YES;
}

- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures
{
    UIViewController *j = [self.camada controladorDoJogo];
    return j ? j.preferredScreenEdgesDeferringSystemGestures : UIRectEdgeAll;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    UIViewController *j = [self.camada controladorDoJogo];
    return j ? j.supportedInterfaceOrientations : UIInterfaceOrientationMaskLandscape;
}

@end

/* ---------- a camada ---------- */

@implementation TQCamada {
    tq_nucleo _n;
    tq_estado_controle _s;
    TQJanela *_janela;
    TQControlador *_vc;
    TQVista *_vista;
    CADisplayLink *_link;
    NSTimer *_espera;
    TQVisivel _visivel;
    BOOL _montada, _forcar;
    int _outros;
    double _proxContagem, _proxTentativa, _proxPreferencias;
    CGRect _ultimoQuadro;
    UIEdgeInsets _ultimasMargens;
    /* ajustes só da parte de iOS */
    float _opacidade;
    NSInteger _vibracao;
    uint8_t _trocas[TQ_ACAO_COUNT]; /* as reatribuições que você fez; TQ_PADRAO = segue o padrão */
    /* mover os botões: como estava antes, para Cancelar e para voltar à tela de antes */
    BOOL _editando;
    tq_posicao _posAntes[TQ_TELAS][TQ_C_COUNT];
    int _modoAntes, _direcaoAntes;
    /* vibração */
    UIImpactFeedbackGenerator *_leve;
    CHHapticEngine *_motor;
    id<CHHapticAdvancedPatternPlayer> _jogador;
    BOOL _motorOk, _motorParado, _tocando;
    double _motorDeNovo, _testeAte;
    float _forcaEnviada, _agudezaEnviada;
    NSString *_erroVibracao;
    /* inclinação */
    CMMotionManager *_movimento;
    BOOL _calibrado;
    float _reto, _anguloAtual;
    UIInterfaceOrientation _orientacao;
}

+ (instancetype)compartilhada
{
    static TQCamada *c;
    static dispatch_once_t uma;
    dispatch_once(&uma, ^{
        c = [[TQCamada alloc] init];
    });
    return c;
}

- (instancetype)init
{
    if ((self = [super init])) {
        _visivel = (TQVisivel)-1;
        tq_ajustes aj;
        tq_ajustes_padrao(&aj);
        tq_iniciar(&_n, &aj);
        _opacidade = 1;
        _forcaEnviada = -1;
        memset(_trocas, TQ_PADRAO, sizeof _trocas);
    }
    return self;
}

/* ----- começo ----- */

- (void)comecar
{
    if (_espera || _montada)
        return;
    ps_carregar();
    [self lerAjustes];
    [self lerPosicoes];
    __weak TQCamada *fraca = self;
    _espera = [NSTimer timerWithTimeInterval:0.25
                                     repeats:YES
                                       block:^(NSTimer *t) {
                                           (void)t;
                                           [fraca tentarMontar];
                                       }];
    [NSRunLoop.mainRunLoop addTimer:_espera forMode:NSRunLoopCommonModes];
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(vaiSairDaFrente:)
                                               name:UIApplicationWillResignActiveNotification
                                             object:nil];
}

- (UIWindowScene *)cena
{
    for (UIScene *s in UIApplication.sharedApplication.connectedScenes)
        if ([s isKindOfClass:UIWindowScene.class] && (s.activationState == UISceneActivationStateForegroundActive ||
                                                     s.activationState == UISceneActivationStateForegroundInactive))
            return (UIWindowScene *)s;
    return nil;
}

/* A janela do jogo (a do SDL): a de nível normal com um controlador, que não é esta. */
- (UIViewController *)controladorDoJogo
{
    UIWindowScene *cena = _janela.windowScene ?: [self cena];
    for (UIWindow *w in cena.windows)
        if (w != _janela && !w.hidden && w.rootViewController && w.windowLevel == UIWindowLevelNormal)
            return w.rootViewController;
    return nil;
}

- (void)tentarMontar
{
    UIWindowScene *cena = [self cena];
    if (!cena || ![self controladorDoJogo])
        return;
    [_espera invalidate];
    _espera = nil;
    _montada = YES;

    _vc = [[TQControlador alloc] init];
    _vc.camada = self;
    _janela = [[TQJanela alloc] initWithWindowScene:cena];
    _janela.windowLevel = UIWindowLevelNormal + 1;
    _janela.backgroundColor = UIColor.clearColor;
    _janela.rootViewController = _vc;
    _vista = (TQVista *)_vc.view;
    _vista.nucleo = &_n;
    _vista.opacidade = _opacidade;
    __weak TQCamada *fraca = self;
    _vista.painel.aoEscolherTela = ^(int tela) {
        [fraca moverNaTela:tela];
    };
    _vista.painel.aoTocar = ^(int botao) {
        [fraca painelTocou:botao];
    };
    _janela.hidden = NO;
    _leve = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];

    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(passo:)];
    _link.preferredFrameRateRange = CAFrameRateRangeMake(30, 60, 60);
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    [self layoutMudou];
    [self passo:_link];
}

- (void)layoutMudou
{
    if (!_vista)
        return;
    const CGRect b = _vista.bounds;
    const UIEdgeInsets m = _vista.safeAreaInsets;
    if (b.size.width <= 0 || b.size.height <= 0)
        return;
    if (CGRectEqualToRect(b, _ultimoQuadro) && UIEdgeInsetsEqualToEdgeInsets(m, _ultimasMargens))
        return;
    _ultimoQuadro = b;
    _ultimasMargens = m;
    tq_layout(&_n, (float)b.size.width, (float)b.size.height, (float)m.left, (float)m.top, (float)m.right,
              (float)m.bottom);
    [_vista refazer];
}

/* ----- a cada quadro ----- */

- (void)passo:(CADisplayLink *)link
{
    (void)link;
    const double t = CACurrentMediaTime();
    [self avaliar:t];
    if (_visivel == TQVisivelTudo && !_editando && _n.modo == TQ_MODO_PILOTAGEM && _n.aj.direcao == TQ_DIRECAO_INCLINACAO)
        [self lerInclinacao];
    else
        [self pararInclinacao];
    [self passoAgora:t];
    [self atualizarVibracao:t];
    if (t >= _proxPreferencias) {
        _proxPreferencias = t + 1.0;
        [_vc setNeedsStatusBarAppearanceUpdate];
        [_vc setNeedsUpdateOfHomeIndicatorAutoHidden];
        [_vc setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
    }
}

/* O núcleo, o envio ao controle virtual e o desenho. Também logo depois de cada toque (menos espera). */
- (void)passoAgora:(double)t
{
    tq_tick(&_n, t, &_s); /* movendo os botões, sai tudo solto */
    if (_visivel == TQVisivelTudo) {
        ps_enviar(&_s);
    } else {
        tq_estado_controle neutro;
        memset(&neutro, 0, sizeof neutro);
        ps_enviar(&neutro);
    }
    const uint32_t ev = tq_eventos(&_n);
    if ((ev & TQ_EV_TOQUE) && _vibracao != TQVibracaoNada)
        [_leve impactOccurred];
    if (ev & TQ_EV_RETO) {
        _calibrado = NO;
        [_vista avisar:@"O reto agora é como o iPhone está." por:2];
    }
    if ((ev & TQ_EV_MODO) && _n.modo == TQ_MODO_PILOTAGEM)
        _calibrado = NO; /* inclinação: o "reto" é como o iPhone está quando começa a pilotar */
    if (ev & TQ_EV_DIRECAO) {
        /* o botão de direção (NFSU2): guarda e avisa */
        [self salvarAjustes];
        _calibrado = NO;
        if (_n.aj.direcao == TQ_DIRECAO_INCLINACAO)
            [_vista avisar:@"Direção: inclinar. Segure o iPhone como um volante; o botão ao lado acerta o reto." por:5];
        else
            [_vista avisar:_n.aj.direcao == TQ_DIRECAO_ANALOGICO ? @"Direção: analógico" : @"Direção: botões ◀ ▶" por:2];
    }
    if (ev & (TQ_EV_LAYOUT | TQ_EV_MODO))
        [_vista refazer];
    else
        [_vista atualizarApertos];
    if (_editando)
        [_vista atualizarPainel];
    if (ev & TQ_EV_AJUSTES)
        [self abrirAjustes];
}

- (void)avaliar:(double)t
{
    const BOOL completo = ps_funcoes_achadas() == ps_funcoes_total();
    const BOOL pronto = completo && ps_sdl_pronto();
    if (!pronto)
        _outros = 0;
    else if (t >= _proxContagem) {
        _outros = ps_outros_controles();
        _proxContagem = t + 0.5;
    }
    const BOOL querControle = pronto && _outros == 0;
    if (querControle && !ps_conectado()) {
        if (t >= _proxTentativa && !ps_conectar())
            _proxTentativa = t + 2.0;
    } else if (!querControle && ps_conectado()) {
        /* um controle de verdade chegou: o virtual sai e o jogo passa a usar o de verdade */
        tq_soltar_tudo(&_n);
        tq_estado_controle neutro;
        memset(&neutro, 0, sizeof neutro);
        ps_enviar(&neutro);
        ps_desconectar();
    }

    TQVisivel v;
    if (_vc.presentedViewController)
        v = _visivel < TQVisivelEngrenagem ? TQVisivelEngrenagem : _visivel; /* a folha daqui aberta: fica como está */
    else if ([self controladorDoJogo].presentedViewController)
        v = TQVisivelNada; /* o jogo abriu algo do iOS (escolher o arquivo do jogo, compartilhar) */
    else if (_editando)
        v = TQVisivelTudo; /* movendo os botões: todos à mostra */
    else if (!pronto || !ps_conectado())
        v = ps_ajustes_abertos() ? TQVisivelNada : TQVisivelEngrenagem; /* falta algo, ou há um controle de verdade */
    else if (_forcar)
        v = TQVisivelTudo; /* Diagnóstico: "Controles sempre à mostra" */
    else if (ps_ajustes_abertos())
        v = TQVisivelNada;
    else if (ps_tela_cheia())
        v = TQVisivelEngrenagem; /* instalador, avisos: o SpeedBreaker usa o toque direto */
    else
        v = TQVisivelTudo;
    if (v != _visivel)
        [self mudarVisivel:v];
}

- (void)mudarVisivel:(TQVisivel)v
{
    if (_visivel == TQVisivelTudo)
        tq_soltar_tudo(&_n);
    _visivel = v;
    _janela.hidden = v == TQVisivelNada;
    _vista.visivel = v;
    [_vista refazer];
    _link.preferredFrameRateRange = v == TQVisivelTudo ? CAFrameRateRangeMake(30, 60, 60) : CAFrameRateRangeMake(10, 15, 15);
}

- (void)vaiSairDaFrente:(NSNotification *)nota
{
    (void)nota;
    tq_soltar_tudo(&_n);
    tq_estado_controle neutro;
    memset(&neutro, 0, sizeof neutro);
    ps_enviar(&neutro);
    [self pararVibracao];
    [_vista refazer];
}

/* ----- toques ----- */

- (BOOL)aceitaToqueEm:(CGPoint)p
{
    if (_visivel == TQVisivelNada || _visivel == (TQVisivel)-1)
        return NO;
    if (_editando)
        return YES;
    const int c = tq_controle_em(&_n, (float)p.x, (float)p.y);
    if (_visivel == TQVisivelEngrenagem)
        return c == TQ_C_AJUSTES;
    return c >= 0;
}

- (void)toques:(NSSet<UITouch *> *)toques fase:(int)fase
{
    const double t = CACurrentMediaTime();
    for (UITouch *toque in toques) {
        const uintptr_t id = (uintptr_t)(__bridge void *)toque;
        const CGPoint p = [toque locationInView:_vista];
        if (fase == 0) {
            if (!_editando && _visivel == TQVisivelEngrenagem && tq_controle_em(&_n, (float)p.x, (float)p.y) != TQ_C_AJUSTES)
                continue;
            [_leve prepare];
            tq_toque_comecou(&_n, id, (float)p.x, (float)p.y, t);
        } else if (fase == 1) {
            tq_toque_moveu(&_n, id, (float)p.x, (float)p.y, t);
        } else {
            tq_toque_acabou(&_n, id, t);
        }
    }
    [self passoAgora:t];
}

/* ----- mover os botões ----- */

- (void)comecarAMover
{
    if (_editando || !_vista)
        return;
    if (_vc.presentedViewController)
        [_vc dismissViewControllerAnimated:NO completion:nil];
    memcpy(_posAntes, _n.pos, sizeof _posAntes);
    _modoAntes = _n.modo;
    _direcaoAntes = _n.aj.direcao;
    _editando = YES;
    tq_editar(&_n, 1);
    _vista.editando = YES;
    [self avaliar:CACurrentMediaTime()];
    [_vista refazer];
}

/* O painel trocou a tela: mostra os botões dela (o modo e o jeito de dirigir voltam ao sair). */
- (void)moverNaTela:(int)tela
{
    if (!_editando)
        return;
    tq_ir_para_tela(&_n, tela);
    [_vista refazer];
}

- (BOOL)mudouAlgo
{
    return memcmp(_posAntes, _n.pos, sizeof _posAntes) != 0;
}

- (void)painelTocou:(int)botao
{
    __weak TQCamada *fraca = self;
    if (botao == 2) {
        [self terminarDeMover:YES];
        return;
    }
    if (botao == 1) {
        /* Padrão: só a tela que está à mostra, e só depois de "Tem certeza?" */
        const int tela = tq_tela_atual(&_n);
        NSString *msg = [NSString stringWithFormat:@"Os botões da tela %@ voltam ao lugar e ao tamanho padrão. "
                                                   @"Nada fica salvo até você tocar em Pronto.",
                                                   texto(tq_nome_tela(tela))];
        UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Tem certeza?"
                                                                   message:msg
                                                            preferredStyle:UIAlertControllerStyleAlert];
        [a addAction:[UIAlertAction actionWithTitle:@"Voltar ao padrão"
                                              style:UIAlertActionStyleDestructive
                                            handler:^(UIAlertAction *x) {
                                                (void)x;
                                                [fraca restaurarTela:tela];
                                            }]];
        [a addAction:[UIAlertAction actionWithTitle:@"Cancelar" style:UIAlertActionStyleCancel handler:nil]];
        [self mostrar:a];
        return;
    }
    if (![self mudouAlgo]) {
        [self terminarDeMover:NO];
        return;
    }
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Tem certeza?"
                                                               message:@"As mudanças de agora nos botões vão ser descartadas."
                                                        preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"Descartar"
                                          style:UIAlertActionStyleDestructive
                                        handler:^(UIAlertAction *x) {
                                            (void)x;
                                            [fraca terminarDeMover:NO];
                                        }]];
    [a addAction:[UIAlertAction actionWithTitle:@"Continuar movendo" style:UIAlertActionStyleCancel handler:nil]];
    [self mostrar:a];
}

- (void)restaurarTela:(int)tela
{
    tq_restaurar_tela(&_n, tela);
    [_vista refazer];
}

- (void)terminarDeMover:(BOOL)salvar
{
    if (!_editando)
        return;
    if (salvar)
        [self salvarPosicoes];
    else
        memcpy(_n.pos, _posAntes, sizeof _posAntes);
    _editando = NO;
    tq_editar(&_n, 0);
    _vista.editando = NO;
    /* volta ao modo e ao jeito de dirigir de antes (o painel pode ter mostrado outra tela) */
    tq_ajustes aj = _n.aj;
    aj.direcao = _direcaoAntes;
    tq_definir_ajustes(&_n, &aj);
    tq_definir_modo(&_n, _modoAntes);
    [_vista refazer];
    if (salvar)
        [_vista avisar:@"Posições salvas." por:1.5];
}

- (void)salvarPosicoes
{
    NSMutableDictionary<NSString *, NSArray<NSNumber *> *> *d = [NSMutableDictionary dictionary];
    for (int tela = 0; tela < TQ_TELAS; tela++)
        for (int c = 0; c < TQ_C_COUNT; c++) {
            const tq_posicao p = tq_posicao_de(&_n, tela, c);
            if (fabsf(p.dx) < 0.25f && fabsf(p.dy) < 0.25f && fabsf(p.tamanho - 1) < 0.005f)
                continue;
            NSString *chave = [NSString stringWithFormat:@"%@.%@", texto(tq_id_tela(tela)), texto(tq_id_controle(c))];
            d[chave] = @[ @(p.dx), @(p.dy), @(p.tamanho) ];
        }
    NSUserDefaults *u = NSUserDefaults.standardUserDefaults;
    if (d.count)
        [u setObject:d forKey:K_POSICOES];
    else
        [u removeObjectForKey:K_POSICOES];
}

- (void)lerPosicoes
{
    for (int tela = 0; tela < TQ_TELAS; tela++)
        tq_restaurar_tela(&_n, tela);
    NSDictionary *d = [NSUserDefaults.standardUserDefaults dictionaryForKey:K_POSICOES];
    for (id chave in d) {
        if (![chave isKindOfClass:NSString.class])
            continue;
        NSArray *partes = [(NSString *)chave componentsSeparatedByString:@"."];
        NSArray *v = d[chave];
        if (partes.count != 2 || ![v isKindOfClass:NSArray.class] || v.count != 3)
            continue;
        const int tela = tq_tela_por_id([partes[0] UTF8String]), c = tq_controle_por_id([partes[1] UTF8String]);
        if (tela < 0 || c < 0)
            continue;
        BOOL numeros = YES;
        for (id x in v)
            if (![x isKindOfClass:NSNumber.class])
                numeros = NO;
        if (!numeros)
            continue;
        const tq_posicao p = { [v[0] floatValue], [v[1] floatValue], [v[2] floatValue] };
        tq_definir_posicao(&_n, tela, c, p); /* corrige o que vier estragado */
    }
}

- (int)posicoesMudadasNaTela:(int)tela
{
    int k = 0;
    for (int c = 0; c < TQ_C_COUNT; c++) {
        const tq_posicao p = tq_posicao_de(&_n, tela, c);
        if (fabsf(p.dx) >= 0.25f || fabsf(p.dy) >= 0.25f || fabsf(p.tamanho - 1) >= 0.005f)
            k++;
    }
    return k;
}

/* ----- inclinação ----- */

- (void)lerInclinacao
{
    if (!_movimento) {
        _movimento = [[CMMotionManager alloc] init];
        _movimento.deviceMotionUpdateInterval = 1.0 / 60;
    }
    if (!_movimento.deviceMotionAvailable)
        return;
    if (!_movimento.deviceMotionActive) {
        [_movimento startDeviceMotionUpdates];
        _calibrado = NO;
    }
    CMDeviceMotion *dm = _movimento.deviceMotion;
    if (!dm)
        return;
    const UIInterfaceOrientation o = _janela.windowScene.effectiveGeometry.interfaceOrientation;
    if (o != _orientacao) {
        _orientacao = o; /* virou o iPhone de lado: o "reto" muda 180 graus */
        _calibrado = NO;
    }
    /* Ângulo da gravidade no plano da tela. Virar o iPhone como um volante para a direita (horário, olhando
     * para a tela) aumenta esse ângulo nos dois jeitos de deitar o iPhone. */
    const float angulo = (float)(atan2(dm.gravity.y, dm.gravity.x) * (180.0 / PI_));
    _anguloAtual = angulo;
    if (!_calibrado) {
        _reto = angulo;
        _calibrado = YES;
    }
    tq_definir_inclinacao(&_n, tq_inclinacao_relativa(angulo, _reto));
}

- (void)pararInclinacao
{
    if (_movimento.deviceMotionActive)
        [_movimento stopDeviceMotionUpdates];
    tq_definir_inclinacao(&_n, 0);
}

/* ----- vibração ----- */

- (BOOL)motorPronto:(double)t
{
    if (_motorOk && !_motorParado)
        return YES;
    if (t < _motorDeNovo)
        return NO;
    _motorDeNovo = t + 5.0; /* se falhar, só tenta de novo daqui a 5 s */
    if (![CHHapticEngine capabilitiesForHardware].supportsHaptics) {
        _erroVibracao = @"este aparelho não tem o motor de vibração do Core Haptics";
        return NO;
    }
    NSError *erro = nil;
    if (_motorOk && _motorParado) {
        if (![_motor startAndReturnError:&erro]) {
            _erroVibracao = erro.localizedDescription;
            return NO;
        }
        _motorParado = NO;
        _tocando = NO;
        return YES;
    }
    _motor = [[CHHapticEngine alloc] initAndReturnError:&erro];
    if (!_motor) {
        _erroVibracao = erro.localizedDescription;
        return NO;
    }
    _motor.playsHapticsOnly = YES;
    _motor.autoShutdownEnabled = YES;
    __weak TQCamada *fraca = self;
    _motor.resetHandler = ^{
        dispatch_async(dispatch_get_main_queue(), ^{
            [fraca motorReiniciou];
        });
    };
    _motor.stoppedHandler = ^(CHHapticEngineStoppedReason motivo) {
        (void)motivo;
        dispatch_async(dispatch_get_main_queue(), ^{
            [fraca motorParou];
        });
    };
    if (![_motor startAndReturnError:&erro]) {
        _erroVibracao = erro.localizedDescription;
        _motor = nil;
        return NO;
    }
    CHHapticEventParameter *forca = [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity
                                                                                  value:1.0f];
    CHHapticEventParameter *agudeza = [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness
                                                                                    value:0.35f];
    CHHapticEvent *continuo = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                                            parameters:@[ forca, agudeza ]
                                                          relativeTime:0
                                                              duration:30];
    CHHapticPattern *padrao = [[CHHapticPattern alloc] initWithEvents:@[ continuo ] parameters:@[] error:&erro];
    if (padrao)
        _jogador = [_motor createAdvancedPlayerWithPattern:padrao error:&erro];
    if (!_jogador) {
        _erroVibracao = erro.localizedDescription ?: @"não deu para criar o padrão de vibração";
        _motor = nil;
        return NO;
    }
    _jogador.loopEnabled = YES;
    _motorOk = YES;
    _motorParado = NO;
    _tocando = NO;
    _erroVibracao = nil;
    return YES;
}

- (void)motorReiniciou
{
    /* o iOS reiniciou o servidor de vibração: tudo é criado de novo na próxima vez */
    _motorOk = NO;
    _tocando = NO;
    _jogador = nil;
    _motor = nil;
    _motorDeNovo = 0;
}

- (void)motorParou
{
    _motorParado = YES;
    _tocando = NO;
    _motorDeNovo = 0;
}

- (void)pararVibracao
{
    if (_tocando)
        [_jogador stopAtTime:CHHapticTimeImmediate error:nil];
    _tocando = NO;
    _testeAte = 0;
}

- (void)atualizarVibracao:(double)t
{
    uint16_t grave, agudo;
    uint32_t pedidos;
    ps_rumble(&grave, &agudo, &pedidos);
    float forca = 0, agudeza = 0;
    if (_vibracao == TQVibracaoTudo && _visivel == TQVisivelTudo && !_editando) {
        const float g = grave / 65535.0f, a = agudo / 65535.0f;
        forca = fmaxf(g, a) * 0.8f;
        agudeza = g + a > 0 ? (a / (g + a) - 0.5f) * 0.6f : 0;
    }
    if (t < _testeAte) {
        forca = 1;
        agudeza = 0;
    }
    forca = roundf(forca * 20) / 20; /* degraus de 5%: menos mensagens para o motor */
    agudeza = roundf(agudeza * 10) / 10;
    if (forca < 0.05f) {
        if (_tocando)
            [self pararVibracao];
        return;
    }
    if (![self motorPronto:t])
        return;
    NSError *erro = nil;
    if (!_tocando) {
        if (![_jogador startAtTime:CHHapticTimeImmediate error:&erro]) {
            _erroVibracao = erro.localizedDescription;
            _motorOk = NO;
            _motorDeNovo = t + 5.0;
            return;
        }
        _tocando = YES;
        _forcaEnviada = -1;
    }
    if (forca != _forcaEnviada || agudeza != _agudezaEnviada) {
        CHHapticDynamicParameter *pf =
            [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
                                                            value:forca
                                                     relativeTime:0];
        CHHapticDynamicParameter *pa =
            [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticSharpnessControl
                                                            value:agudeza
                                                     relativeTime:0];
        [_jogador sendParameters:@[ pf, pa ] atTime:CHHapticTimeImmediate error:&erro];
        _forcaEnviada = forca;
        _agudezaEnviada = agudeza;
    }
}

/* ----- ajustes ----- */

- (void)lerAjustes
{
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    tq_ajustes aj;
    tq_ajustes_padrao(&aj);
    if ([d objectForKey:K_DIRECAO])
        aj.direcao = (int)[d integerForKey:K_DIRECAO];
    if ([d objectForKey:K_ESCALA])
        aj.escala = [d floatForKey:K_ESCALA];
    if ([d objectForKey:K_CAMBIO])
        aj.cambio_manual = [d boolForKey:K_CAMBIO];
    if ([d objectForKey:K_PAUSA])
        aj.pausa_vai_para_menu = [d boolForKey:K_PAUSA];
    /* Os botões: o padrão desta build, mais o que você reatribuiu. Assim, um padrão corrigido numa build nova (como o
     * Mapa e o SMS) chega sozinho, e o que você trocou fica. */
    memset(_trocas, TQ_PADRAO, sizeof _trocas);
    NSData *trocas = [d dataForKey:K_TROCAS];
    NSData *antigo = [d dataForKey:K_MAPA_ANTIGO];
    if (trocas.length == TQ_ACAO_COUNT) {
        memcpy(_trocas, trocas.bytes, TQ_ACAO_COUNT);
    } else if (antigo.length == TQ_ACAO_COUNT) {
        const uint8_t *v = antigo.bytes;
        for (int i = 0; i < TQ_ACAO_COUNT; i++)
            if (v[i] != MAPA_PADRAO_ENTREGA2[i])
                _trocas[i] = v[i];
    }
    for (int i = 0; i < TQ_ACAO_COUNT; i++)
        if (_trocas[i] != TQ_PADRAO && _trocas[i] < TQ_O_COUNT)
            aj.mapa[i] = _trocas[i];
        else
            _trocas[i] = TQ_PADRAO;
    tq_definir_ajustes(&_n, &aj); /* corrige o que vier estragado */
    _opacidade = [d objectForKey:K_OPACIDADE] ? [d floatForKey:K_OPACIDADE] : 1.0f;
    if (!(_opacidade >= 0.3f && _opacidade <= 1.0f))
        _opacidade = 1.0f;
    _vibracao = [d objectForKey:K_VIBRACAO] ? [d integerForKey:K_VIBRACAO] : TQVibracaoTudo;
    if (_vibracao < TQVibracaoTudo || _vibracao > TQVibracaoNada)
        _vibracao = TQVibracaoTudo;
}

- (void)salvarAjustes
{
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    [d setInteger:_n.aj.direcao forKey:K_DIRECAO];
    [d setFloat:_n.aj.escala forKey:K_ESCALA];
    [d setBool:_n.aj.cambio_manual != 0 forKey:K_CAMBIO];
    [d setBool:_n.aj.pausa_vai_para_menu != 0 forKey:K_PAUSA];
    [d setObject:[NSData dataWithBytes:_trocas length:TQ_ACAO_COUNT] forKey:K_TROCAS];
    [d removeObjectForKey:K_MAPA_ANTIGO]; /* já migrado */
    [d setFloat:_opacidade forKey:K_OPACIDADE];
    [d setInteger:_vibracao forKey:K_VIBRACAO];
}

/* muda um ajuste do núcleo, salva, redesenha e volta para a folha de ajustes */
- (void)mudar:(void (^)(tq_ajustes *aj))mudanca
{
    tq_ajustes aj = _n.aj;
    mudanca(&aj);
    tq_soltar_tudo(&_n);
    tq_definir_ajustes(&_n, &aj);
    _vista.opacidade = _opacidade;
    [self salvarAjustes];
    [_vista refazer];
    [self abrirAjustes];
}

- (void)mostrar:(UIAlertController *)a
{
    UIPopoverPresentationController *pop = a.popoverPresentationController;
    if (pop) {
        pop.sourceView = _vista;
        pop.sourceRect = [_vista quadroDaEngrenagem];
    }
    if (_vc.presentedViewController)
        [_vc dismissViewControllerAnimated:NO completion:nil];
    [_vc presentViewController:a animated:YES completion:nil];
}

- (UIAlertAction *)acao:(NSString *)titulo faz:(void (^)(void))faz
{
    return [UIAlertAction actionWithTitle:titulo
                                    style:UIAlertActionStyleDefault
                                  handler:^(UIAlertAction *a) {
                                      (void)a;
                                      faz();
                                  }];
}

- (UIAlertAction *)voltar
{
    __weak TQCamada *fraca = self;
    return [UIAlertAction actionWithTitle:@"Voltar"
                                    style:UIAlertActionStyleCancel
                                  handler:^(UIAlertAction *a) {
                                      (void)a;
                                      [fraca abrirAjustes];
                                  }];
}

- (void)abrirAjustes
{
    if (!_vc || _editando)
        return;
    tq_soltar_tudo(&_n);
    __weak TQCamada *fraca = self;
    NSString *msg = [NSString stringWithFormat:@"Controles por toque, build %@.\nMapa, SMS e as marchas ainda vão ser "
                                               @"confirmados no jogo: se algum não funcionar, use Reatribuir botões.",
                                               texto(TQ_BUILD)];
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Toque"
                                                               message:msg
                                                        preferredStyle:UIAlertControllerStyleActionSheet];
    [a addAction:[self acao:@"Mover os botões"
                        faz:^{
                            [fraca comecarAMover];
                        }]];
    [a addAction:[self acao:[@"Direção: " stringByAppendingString:nomeDirecao(_n.aj.direcao)]
                        faz:^{
                            [fraca escolherDirecao];
                        }]];
    [a addAction:[self acao:[@"Tamanho: " stringByAppendingString:porcento(_n.aj.escala)]
                        faz:^{
                            [fraca escolherTamanho];
                        }]];
    [a addAction:[self acao:[@"Opacidade: " stringByAppendingString:porcento(_opacidade)]
                        faz:^{
                            [fraca escolherOpacidade];
                        }]];
    [a addAction:[self acao:[@"Vibração: " stringByAppendingString:nomeVibracao(_vibracao)]
                        faz:^{
                            [fraca escolherVibracao];
                        }]];
    [a addAction:[self acao:_n.aj.cambio_manual ? @"Câmbio manual (Marcha + e −): sim" : @"Câmbio manual (Marcha + e −): não"
                        faz:^{
                            [fraca mudar:^(tq_ajustes *aj) {
                                aj->cambio_manual = !aj->cambio_manual;
                            }];
                        }]];
    [a addAction:[self acao:_n.aj.pausa_vai_para_menu ? @"Pausa muda para o modo Menu: sim"
                                                      : @"Pausa muda para o modo Menu: não"
                        faz:^{
                            [fraca mudar:^(tq_ajustes *aj) {
                                aj->pausa_vai_para_menu = !aj->pausa_vai_para_menu;
                            }];
                        }]];
    [a addAction:[self acao:@"Reatribuir botões"
                        faz:^{
                            [fraca escolherAcaoParaReatribuir];
                        }]];
    [a addAction:[self acao:@"Abrir os ajustes do SpeedBreaker"
                        faz:^{
                            [fraca abrirAjustesDoSpeedBreaker];
                        }]];
    [a addAction:[self acao:@"Diagnóstico"
                        faz:^{
                            [fraca abrirDiagnostico];
                        }]];
    [a addAction:[UIAlertAction actionWithTitle:@"Restaurar o padrão"
                                          style:UIAlertActionStyleDestructive
                                        handler:^(UIAlertAction *x) {
                                            (void)x;
                                            [fraca confirmarRestaurar];
                                        }]];
    [a addAction:[UIAlertAction actionWithTitle:@"Fechar" style:UIAlertActionStyleCancel handler:nil]];
    [self mostrar:a];
}

- (void)escolherDirecao
{
    __weak TQCamada *fraca = self;
    UIAlertController *a = [UIAlertController
        alertControllerWithTitle:@"Direção"
                         message:@"O botão no canto de cima à direita também troca. Inclinar: vire o iPhone como um "
                                 @"volante; o jeito que ele estiver quando você começar a pilotar conta como reto (o "
                                 @"botão ao lado acerta de novo)."
                  preferredStyle:UIAlertControllerStyleActionSheet];
    const int opcoes[] = { TQ_DIRECAO_BOTOES, TQ_DIRECAO_ANALOGICO, TQ_DIRECAO_INCLINACAO };
    for (int i = 0; i < 3; i++) {
        const int d = opcoes[i];
        NSString *t = d == _n.aj.direcao ? [@"✓ " stringByAppendingString:nomeDirecao(d)] : nomeDirecao(d);
        [a addAction:[self acao:t
                            faz:^{
                                [fraca mudar:^(tq_ajustes *aj) {
                                    aj->direcao = d;
                                }];
                            }]];
    }
    [a addAction:[self voltar]];
    [self mostrar:a];
}

/* O maior tamanho que cabe nesta tela (o núcleo limita a ele): 608 x 324 pontos no 100%, entre as margens. */
- (float)tamanhoQueCabe
{
    if (_n.w <= 0 || _n.h <= 0)
        return 1.5f;
    const float l = fmaxf(12, _n.se), r = fmaxf(12, _n.sd), t = fmaxf(12, _n.st), b = fmaxf(12, _n.sb);
    return fminf((_n.w - l - r) / 608.0f, (_n.h - t - b) / 324.0f);
}

- (void)escolherTamanho
{
    __weak TQCamada *fraca = self;
    const float cabe = [self tamanhoQueCabe];
    NSString *msg = cabe < 1.5f ? [NSString stringWithFormat:@"Nesta tela cabem até %d%%: acima disso, fica nesse.",
                                                             (int)floorf(cabe * 100)]
                                : nil;
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Tamanho dos controles"
                                                               message:msg
                                                        preferredStyle:UIAlertControllerStyleActionSheet];
    for (int i = 0; i < N_ESCALAS; i++) {
        const float e = ESCALAS[i];
        NSString *t = fabsf(e - _n.aj.escala) < 0.01f ? [@"✓ " stringByAppendingString:porcento(e)] : porcento(e);
        [a addAction:[self acao:t
                            faz:^{
                                [fraca mudar:^(tq_ajustes *aj) {
                                    aj->escala = e;
                                }];
                            }]];
    }
    [a addAction:[self voltar]];
    [self mostrar:a];
}

- (void)escolherOpacidade
{
    __weak TQCamada *fraca = self;
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Opacidade"
                                                               message:@"100% é o desenho do NFSU2, já meio transparente. "
                                                                       @"Menos deixa os controles mais apagados."
                                                        preferredStyle:UIAlertControllerStyleActionSheet];
    for (int i = 0; i < N_OPACIDADES; i++) {
        const float v = OPACIDADES[i];
        NSString *t = fabsf(v - _opacidade) < 0.01f ? [@"✓ " stringByAppendingString:porcento(v)] : porcento(v);
        [a addAction:[self acao:t
                            faz:^{
                                [fraca definirOpacidade:v];
                            }]];
    }
    [a addAction:[self voltar]];
    [self mostrar:a];
}

- (void)definirOpacidade:(float)v
{
    _opacidade = v;
    [self mudar:^(tq_ajustes *aj) {
        (void)aj;
    }];
}

- (void)escolherVibracao
{
    __weak TQCamada *fraca = self;
    UIAlertController *a = [UIAlertController
        alertControllerWithTitle:@"Vibração"
                         message:@"Toques: uma batidinha a cada controle apertado. Jogo: a vibração que o jogo "
                                 @"manda para o controle (batidas, motor)."
                  preferredStyle:UIAlertControllerStyleActionSheet];
    for (NSInteger v = TQVibracaoTudo; v <= TQVibracaoNada; v++) {
        NSString *t = v == _vibracao ? [@"✓ " stringByAppendingString:nomeVibracao(v)] : nomeVibracao(v);
        [a addAction:[self acao:t
                            faz:^{
                                [fraca definirVibracao:v];
                            }]];
    }
    [a addAction:[self voltar]];
    [self mostrar:a];
}

- (void)definirVibracao:(NSInteger)v
{
    _vibracao = v;
    [self pararVibracao];
    [self mudar:^(tq_ajustes *aj) {
        (void)aj;
    }];
}

- (void)escolherAcaoParaReatribuir
{
    __weak TQCamada *fraca = self;
    UIAlertController *a = [UIAlertController
        alertControllerWithTitle:@"Reatribuir botões"
                         message:@"Cada ação aperta um botão do controle de Xbox 360. Escolha a ação para trocar."
                  preferredStyle:UIAlertControllerStyleActionSheet];
    for (int acao = 0; acao < TQ_ACAO_COUNT; acao++) {
        NSString *t = [NSString stringWithFormat:@"%@: %@", texto(tq_nome_acao(acao)), texto(tq_nome_saida(_n.aj.mapa[acao]))];
        [a addAction:[self acao:t
                            faz:^{
                                [fraca escolherSaidaPara:acao];
                            }]];
    }
    [a addAction:[self voltar]];
    [self mostrar:a];
}

- (void)escolherSaidaPara:(int)acao
{
    __weak TQCamada *fraca = self;
    tq_ajustes padrao;
    tq_ajustes_padrao(&padrao);
    UIAlertController *a = [UIAlertController alertControllerWithTitle:texto(tq_nome_acao(acao))
                                                               message:@"Qual botão do controle essa ação aperta?"
                                                        preferredStyle:UIAlertControllerStyleActionSheet];
    for (int saida = 0; saida < TQ_O_COUNT; saida++) {
        NSString *nome = texto(tq_nome_saida(saida));
        if (saida == padrao.mapa[acao])
            nome = [nome stringByAppendingString:@" (padrão)"];
        NSString *t = saida == _n.aj.mapa[acao] ? [@"✓ " stringByAppendingString:nome] : nome;
        const BOOL ehPadrao = saida == padrao.mapa[acao];
        [a addAction:[self acao:t
                            faz:^{
                                [fraca reatribuir:acao para:saida padrao:ehPadrao];
                            }]];
    }
    [a addAction:[UIAlertAction actionWithTitle:@"Voltar"
                                          style:UIAlertActionStyleCancel
                                        handler:^(UIAlertAction *x) {
                                            (void)x;
                                            [fraca escolherAcaoParaReatribuir];
                                        }]];
    [self mostrar:a];
}

- (void)reatribuir:(int)acao para:(int)saida padrao:(BOOL)ehPadrao
{
    if (acao < 0 || acao >= TQ_ACAO_COUNT)
        return;
    _trocas[acao] = ehPadrao ? TQ_PADRAO : (uint8_t)saida; /* escolheu o padrão: volta a seguir o padrão */
    [self mudar:^(tq_ajustes *aj) {
        aj->mapa[acao] = (uint8_t)saida;
    }];
}

- (void)abrirAjustesDoSpeedBreaker
{
    /* Back + Start juntos, como num controle: o SpeedBreaker abre os ajustes dele */
    tq_pulso(&_n, (1u << TQ_O_BACK) | (1u << TQ_O_START), CACurrentMediaTime(), 0.25);
}

- (void)confirmarRestaurar
{
    __weak TQCamada *fraca = self;
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Tem certeza?"
                                                               message:@"Volta todos os ajustes dos controles por toque ao "
                                                                       @"padrão, inclusive os botões reatribuídos e as "
                                                                       @"posições e os tamanhos dos botões."
                                                        preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"Restaurar"
                                          style:UIAlertActionStyleDestructive
                                        handler:^(UIAlertAction *x) {
                                            (void)x;
                                            [fraca restaurar];
                                        }]];
    [a addAction:[self voltar]];
    [self mostrar:a];
}

- (void)restaurar
{
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    for (NSString *k in @[ K_DIRECAO, K_ESCALA, K_OPACIDADE, K_TRANSPARENCIA_ANTIGA, K_VIBRACAO, K_CAMBIO, K_PAUSA,
                           K_MAPA_ANTIGO, K_TROCAS, K_POSICOES ])
        [d removeObjectForKey:k];
    tq_soltar_tudo(&_n);
    [self lerAjustes];
    [self lerPosicoes];
    _vista.opacidade = _opacidade;
    [self pararVibracao];
    [_vista refazer];
    [self abrirAjustes];
}

/* ----- diagnóstico ----- */

- (NSString *)textoDiagnostico
{
    char ponte[700];
    ps_status_texto(ponte, sizeof ponte);
    NSMutableString *t = [NSMutableString stringWithFormat:@"Toque build %@\n%@\n", texto(TQ_BUILD), texto(ponte)];
    const double livre = (double)os_proc_available_memory() / (1024.0 * 1024 * 1024);
    const double total = (double)NSProcessInfo.processInfo.physicalMemory / (1024.0 * 1024 * 1024);
    [t appendFormat:@"Memória que o app ainda pode usar: %.2f GB (o iPhone tem %.1f GB)\n", livre, total];
    static NSString *const nomesVisivel[] = { @"escondida", @"só o ≡", @"todos os controles" };
    [t appendFormat:@"Camada: %@%@\n", _visivel >= 0 && _visivel <= 2 ? nomesVisivel[_visivel] : @"?",
                    _forcar ? @" (sempre à mostra)" : @""];
    [t appendFormat:@"Modo: %@. Direção: %@. Tamanho: %@ (em uso %@). Opacidade: %@\n",
                    _n.modo == TQ_MODO_PILOTAGEM ? @"Pilotar" : @"Menu", nomeDirecao(_n.aj.direcao), porcento(_n.aj.escala),
                    porcento(_n.s), porcento(_opacidade)];
    [t appendFormat:@"Botões mudados de lugar: Menu %d, Botões %d, Analógico %d, Inclinação %d\n",
                    [self posicoesMudadasNaTela:TQ_TELA_MENU], [self posicoesMudadasNaTela:TQ_TELA_BOTOES],
                    [self posicoesMudadasNaTela:TQ_TELA_ANALOGICO], [self posicoesMudadasNaTela:TQ_TELA_INCLINACAO]];
    NSMutableString *trocas = [NSMutableString string];
    for (int i = 0; i < TQ_ACAO_COUNT; i++)
        if (_trocas[i] != TQ_PADRAO)
            [trocas appendFormat:@"%@%@ = %@", trocas.length ? @"; " : @"", texto(tq_nome_acao(i)),
                                 texto(tq_nome_saida(_n.aj.mapa[i]))];
    [t appendFormat:@"Botões reatribuídos: %@\n", trocas.length ? trocas : @"nenhum"];
    [t appendFormat:@"Tela: %.0f x %.0f pontos, margens %.0f / %.0f / %.0f / %.0f\n", _n.w, _n.h, _n.se, _n.st, _n.sd,
                    _n.sb];
    [t appendFormat:@"Enviado agora: botões 0x%04x, volante %.2f, freio %.0f, acelerador %.0f\n", (unsigned)_s.botoes,
                    _s.lx, _s.lt, _s.rt];
    if (_movimento.deviceMotionActive)
        [t appendFormat:@"Inclinação: %.1f graus (reto em %.1f)\n", _anguloAtual - _reto, _reto];
    uint16_t grave, agudo;
    uint32_t pedidos;
    ps_rumble(&grave, &agudo, &pedidos);
    [t appendFormat:@"Vibração: %@; o jogo pediu %u vezes (agora %u / %u)%@%@",
                    [CHHapticEngine capabilitiesForHardware].supportsHaptics ? @"o aparelho tem" : @"o aparelho não tem",
                    (unsigned)pedidos, (unsigned)grave, (unsigned)agudo, _erroVibracao ? @"\nErro da vibração: " : @"", _erroVibracao ?: @""];
    return t;
}

- (void)abrirDiagnostico
{
    __weak TQCamada *fraca = self;
    NSString *t = [self textoDiagnostico];
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Diagnóstico"
                                                               message:t
                                                        preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[self acao:@"Atualizar"
                        faz:^{
                            [fraca abrirDiagnostico];
                        }]];
    [a addAction:[self acao:@"Copiar"
                        faz:^{
                            UIPasteboard.generalPasteboard.string = t;
                            [fraca abrirDiagnostico];
                        }]];
    [a addAction:[self acao:@"Testar a vibração (1 s)"
                        faz:^{
                            [fraca testarVibracao];
                        }]];
    [a addAction:[self acao:_forcar ? @"Controles sempre à mostra: sim" : @"Controles sempre à mostra: não"
                        faz:^{
                            [fraca alternarForcar];
                        }]];
    [a addAction:[self voltar]];
    [self mostrar:a];
}

- (void)testarVibracao
{
    _motorDeNovo = 0;
    _testeAte = CACurrentMediaTime() + 1.0;
    [self abrirDiagnostico];
}

- (void)alternarForcar
{
    _forcar = !_forcar; /* não fica salvo: some ao fechar o app */
    [self abrirDiagnostico];
}

@end

/* ---------- entrada ---------- */

/* Roda quando o framework carrega, antes do main do app. Só agenda o começo: o resto espera o app abrir. Os dois
 * caminhos levam ao mesmo comecar, que só age na primeira vez. */
__attribute__((constructor)) static void tq_carregou(void)
{
    @autoreleasepool {
        [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationDidFinishLaunchingNotification
                                                        object:nil
                                                         queue:nil
                                                    usingBlock:^(NSNotification *nota) {
                                                        (void)nota;
                                                        [[TQCamada compartilhada] comecar];
                                                    }];
        dispatch_async(dispatch_get_main_queue(), ^{
            [[TQCamada compartilhada] comecar];
        });
    }
}
