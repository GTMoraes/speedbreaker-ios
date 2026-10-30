/*
 * Toque: a parte de iOS dos controles por toque do NFS Carbon (SpeedBreaker).
 *
 * Entra no app como um framework que o executável carrega (scripts/injetar.py põe um LC_LOAD_WEAK_DYLIB).
 * Não mexe no jogo: cria um controle de Xbox 360 virtual no SDL do próprio app (ponte_sdl.c) e desenha os
 * controles numa janela transparente por cima. Toque fora de um controle passa direto para o jogo.
 *
 * O que decide o quê (posições, toques, modos) está no núcleo em C (nucleo.c, testado em testes/). Aqui fica
 * só o que é de iOS: janela, desenho, toques, vibração, inclinação, ajustes e diagnóstico.
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

typedef NS_ENUM(NSInteger, TQVisivel) {
    TQVisivelNada = 0,       /* janela escondida: ajustes do SpeedBreaker, ou o jogo mostrando algo do iOS */
    TQVisivelEngrenagem = 1, /* só a engrenagem: telas cheias do SpeedBreaker, controle de verdade, falta algo */
    TQVisivelTudo = 2,
};

typedef NS_ENUM(NSInteger, TQVibracao) {
    TQVibracaoTudo = 0,   /* toques e o rumble do jogo */
    TQVibracaoToques = 1, /* só nos toques */
    TQVibracaoNada = 2,
};

static const float ESCALAS[] = { 0.8f, 0.9f, 1.0f, 1.15f, 1.3f, 1.5f };
static const float TRANSPARENCIAS[] = { 0.4f, 0.55f, 0.7f, 0.85f, 1.0f };
#define N_ESCALAS ((int)(sizeof ESCALAS / sizeof ESCALAS[0]))
#define N_TRANSPARENCIAS ((int)(sizeof TRANSPARENCIAS / sizeof TRANSPARENCIAS[0]))

static NSString *const K_DIRECAO = @"toque.direcao";
static NSString *const K_ESCALA = @"toque.escala";
static NSString *const K_TRANSPARENCIA = @"toque.transparencia";
static NSString *const K_VIBRACAO = @"toque.vibracao";
static NSString *const K_CAMBIO = @"toque.cambio_manual";
static NSString *const K_PAUSA = @"toque.pausa_vai_para_menu";
static NSString *const K_MAPA = @"toque.mapa";

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

@class TQCamada;

/* ---------- uma peça desenhada (um controle) ---------- */

@interface TQPeca : UIView
- (instancetype)initWithControle:(int)c;
- (void)posicionar:(const tq_info_controle *)k;
- (void)mostrarApertado:(int)apertado seta:(int)seta;
@end

static UIColor *corDaBorda(int c)
{
    switch (c) {
    case TQ_C_GAS: return [UIColor colorWithRed:0.35 green:0.92 blue:0.45 alpha:0.95];
    case TQ_C_FREIO: return [UIColor colorWithRed:1.0 green:0.42 blue:0.36 alpha:0.95];
    case TQ_C_NITRO: return [UIColor colorWithRed:0.36 green:0.82 blue:1.0 alpha:0.95];
    case TQ_C_SPEEDBREAKER: return [UIColor colorWithRed:1.0 green:0.78 blue:0.3 alpha:0.95];
    case TQ_C_MODO: return [UIColor colorWithRed:1.0 green:0.86 blue:0.36 alpha:0.95];
    default: return [UIColor colorWithWhite:1.0 alpha:0.8];
    }
}

static NSString *simboloDoControle(int c)
{
    switch (c) {
    case TQ_C_AJUSTES: return @"gearshape.fill";
    case TQ_C_PAUSA: return @"pause.fill";
    case TQ_C_ESQUERDA: return @"arrowtriangle.left.fill";
    case TQ_C_DIREITA: return @"arrowtriangle.right.fill";
    default: return nil;
    }
}

@implementation TQPeca {
    int _c;
    CAShapeLayer *_forma;
    UILabel *_rotulo;
    UIImageView *_icone;
    NSArray<CAShapeLayer *> *_setas; /* direcional: cima, baixo, esquerda, direita */
    int _apertado, _seta;
    const char *_rotuloAtual;
    CGRect _quadroAtual;
}

- (instancetype)initWithControle:(int)c
{
    if ((self = [super initWithFrame:CGRectZero])) {
        _c = c;
        _apertado = -1;
        _seta = -2;
        self.userInteractionEnabled = NO; /* quem recebe os toques é a TQVista */
        self.backgroundColor = UIColor.clearColor;
        _forma = [CAShapeLayer layer];
        _forma.strokeColor = corDaBorda(c).CGColor;
        _forma.lineWidth = 2;
        [self.layer addSublayer:_forma];
        NSString *simbolo = simboloDoControle(c);
        if (simbolo) {
            _icone = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:simbolo]];
            _icone.tintColor = [UIColor colorWithWhite:1.0 alpha:0.95];
            _icone.contentMode = UIViewContentModeScaleAspectFit;
            [self addSubview:_icone];
        } else if (c != TQ_C_DPAD) {
            _rotulo = [[UILabel alloc] init];
            _rotulo.textColor = UIColor.whiteColor;
            _rotulo.textAlignment = NSTextAlignmentCenter;
            _rotulo.numberOfLines = 0;
            _rotulo.adjustsFontSizeToFitWidth = YES;
            _rotulo.minimumScaleFactor = 0.5;
            [self addSubview:_rotulo];
        }
        if (c == TQ_C_DPAD) {
            NSMutableArray *setas = [NSMutableArray array];
            for (int i = 0; i < 4; i++) {
                CAShapeLayer *s = [CAShapeLayer layer];
                [self.layer addSublayer:s];
                [setas addObject:s];
            }
            _setas = setas;
        }
    }
    return self;
}

/* triângulo de uma seta do direcional, em volta do centro (r, r) */
static UIBezierPath *caminhoSeta(int i, CGFloat r)
{
    const CGFloat ponta = r * 0.8f, base = r * 0.42f, meia = r * 0.24f;
    CGPoint p[3];
    switch (i) {
    case 0: p[0] = CGPointMake(r, r - ponta); p[1] = CGPointMake(r - meia, r - base); p[2] = CGPointMake(r + meia, r - base); break;
    case 1: p[0] = CGPointMake(r, r + ponta); p[1] = CGPointMake(r - meia, r + base); p[2] = CGPointMake(r + meia, r + base); break;
    case 2: p[0] = CGPointMake(r - ponta, r); p[1] = CGPointMake(r - base, r - meia); p[2] = CGPointMake(r - base, r + meia); break;
    default: p[0] = CGPointMake(r + ponta, r); p[1] = CGPointMake(r + base, r - meia); p[2] = CGPointMake(r + base, r + meia); break;
    }
    UIBezierPath *b = [UIBezierPath bezierPath];
    [b moveToPoint:p[0]];
    [b addLineToPoint:p[1]];
    [b addLineToPoint:p[2]];
    [b closePath];
    return b;
}

- (void)posicionar:(const tq_info_controle *)k
{
    const CGFloat r = k->r;
    const BOOL circulo = k->forma == TQ_FORMA_CIRCULO;
    const CGFloat meiaL = circulo ? r : k->meia_l;
    const CGRect quadro = CGRectMake(k->cx - meiaL, k->cy - r, 2 * meiaL, 2 * r);
    if (!CGRectEqualToRect(quadro, _quadroAtual)) {
        _quadroAtual = quadro;
        self.frame = quadro;
        const CGRect dentro = CGRectInset(self.bounds, 1, 1);
        _forma.frame = self.bounds;
        _forma.path = circulo ? [UIBezierPath bezierPathWithOvalInRect:dentro].CGPath
                              : [UIBezierPath bezierPathWithRoundedRect:dentro cornerRadius:r].CGPath;
        if (_icone) {
            const CGFloat lado = r * (_c == TQ_C_ESQUERDA || _c == TQ_C_DIREITA ? 0.9f : 0.95f);
            _icone.frame = CGRectMake(meiaL - lado / 2, r - lado / 2, lado, lado);
        }
        _rotulo.frame = circulo ? CGRectInset(self.bounds, r * 0.14f, r * 0.14f) : CGRectInset(self.bounds, 4, 1);
        for (int i = 0; i < (int)_setas.count; i++)
            _setas[i].path = caminhoSeta(i, r).CGPath;
        _rotuloAtual = NULL; /* recalcula a fonte */
        _apertado = -1;
    }
    if (_rotulo && k->rotulo != _rotuloAtual) {
        _rotuloAtual = k->rotulo;
        NSString *t = texto(k->rotulo);
        NSArray<NSString *> *linhas = [t componentsSeparatedByString:@"\n"];
        NSUInteger maior = 0;
        for (NSString *l in linhas)
            maior = MAX(maior, l.length);
        CGFloat tam;
        if (!circulo)
            tam = r * 0.95f;
        else if (linhas.count > 1)
            tam = r * 0.36f;
        else if (maior <= 2)
            tam = r * 0.62f;
        else if (maior <= 4)
            tam = r * 0.46f;
        else
            tam = r * 0.34f;
        _rotulo.font = [UIFont systemFontOfSize:MAX(tam, 7) weight:UIFontWeightHeavy];
        _rotulo.text = t;
    }
}

- (void)mostrarApertado:(int)apertado seta:(int)seta
{
    if (apertado != _apertado) {
        _apertado = apertado;
        _forma.fillColor = apertado ? [corDaBorda(_c) colorWithAlphaComponent:0.5].CGColor
                                    : [UIColor colorWithWhite:0.0 alpha:0.3].CGColor;
    }
    if (_setas && seta != _seta) {
        _seta = seta;
        const int acesa = seta == TQ_O_DUP ? 0 : seta == TQ_O_DDOWN ? 1 : seta == TQ_O_DLEFT ? 2 : seta == TQ_O_DRIGHT ? 3 : -1;
        for (int i = 0; i < 4; i++)
            _setas[i].fillColor = [UIColor colorWithWhite:1.0 alpha:i == acesa ? 0.95f : 0.45f].CGColor;
    }
}

@end

/* ---------- a vista com todos os controles ---------- */

@interface TQVista : UIView
@property (nonatomic, weak) TQCamada *camada;
@property (nonatomic, assign) tq_nucleo *nucleo;
@property (nonatomic) TQVisivel visivel;
@property (nonatomic) CGFloat transparencia;
- (void)refazer;
- (void)atualizarApertos;
- (CGRect)quadroDaEngrenagem;
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
    CAShapeLayer *_anel, *_bola; /* analógico */
}

- (instancetype)initWithFrame:(CGRect)quadro
{
    if ((self = [super initWithFrame:quadro])) {
        self.backgroundColor = UIColor.clearColor;
        self.opaque = NO;
        self.multipleTouchEnabled = YES;
        _transparencia = 0.7;
        for (int c = 0; c < TQ_C_COUNT; c++) {
            if (c == TQ_C_ANALOGICO)
                continue;
            _pecas[c] = [[TQPeca alloc] initWithControle:c];
            _pecas[c].hidden = YES;
            [self addSubview:_pecas[c]];
        }
        _anel = [CAShapeLayer layer];
        _anel.fillColor = [UIColor colorWithWhite:0.0 alpha:0.22].CGColor;
        _anel.strokeColor = [UIColor colorWithWhite:1.0 alpha:0.7].CGColor;
        _anel.lineWidth = 2;
        _anel.hidden = YES;
        [self.layer addSublayer:_anel];
        _bola = [CAShapeLayer layer];
        _bola.fillColor = [UIColor colorWithWhite:1.0 alpha:0.55].CGColor;
        _bola.hidden = YES;
        [self.layer addSublayer:_bola];
    }
    return self;
}

- (BOOL)pointInside:(CGPoint)p withEvent:(UIEvent *)e
{
    (void)e;
    return [self.camada aceitaToqueEm:p];
}

- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e
{
    return [self pointInside:p withEvent:e] ? self : nil;
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
            p.alpha = self.visivel == TQVisivelTudo ? self.transparencia : MIN(self.transparencia, 0.5);
        }
    }
    const BOOL analogico = self.visivel == TQVisivelTudo && n->c[TQ_C_ANALOGICO].visivel;
    _anel.hidden = _bola.hidden = !analogico;
    if (analogico) {
        const CGFloat ra = 12 * n->u, rb = 5.5f * n->u;
        _anel.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(-ra, -ra, 2 * ra, 2 * ra)].CGPath;
        _bola.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(-rb, -rb, 2 * rb, 2 * rb)].CGPath;
        _anel.opacity = _bola.opacity = (float)self.transparencia;
    }
    [CATransaction commit];
    [self atualizarApertos];
}

- (void)atualizarApertos
{
    const tq_nucleo *n = self.nucleo;
    if (!n || self.visivel == TQVisivelNada)
        return;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (int c = 0; c < TQ_C_COUNT; c++)
        if (_pecas[c] && !_pecas[c].hidden)
            [_pecas[c] mostrarApertado:n->c[c].apertado seta:c == TQ_C_DPAD ? n->dpad_dir : -1];
    if (!_anel.hidden) {
        const CGFloat ra = 12 * n->u;
        CGFloat dx = n->knob_x - n->base_x, dy = n->knob_y - n->base_y;
        const CGFloat d = hypot(dx, dy);
        if (d > ra) {
            dx *= ra / d;
            dy *= ra / d;
        }
        _anel.position = CGPointMake(n->base_x, n->base_y);
        _bola.position = CGPointMake(n->base_x + dx, n->base_y + dy);
    }
    [CATransaction commit];
}

- (CGRect)quadroDaEngrenagem
{
    TQPeca *p = _pecas[TQ_C_AJUSTES];
    return p && !p.hidden ? p.frame : CGRectMake(CGRectGetMaxX(self.bounds) - 60, 10, 50, 50);
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

/* Só pega o toque que cai num controle (a TQVista decide) ou, com a folha de ajustes aberta, tudo. */
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e
{
    UIView *v = [super hitTest:p withEvent:e];
    if (!v || v == self)
        return nil;
    if (v == self.rootViewController.view)
        return v;
    if (self.rootViewController.presentedViewController)
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
    float _transparencia;
    NSInteger _vibracao;
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
        _transparencia = 0.7f;
        _forcaEnviada = -1;
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
    _vista.transparencia = _transparencia;
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
    if (_visivel == TQVisivelTudo && _n.modo == TQ_MODO_PILOTAGEM && _n.aj.direcao == TQ_DIRECAO_INCLINACAO)
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
    tq_tick(&_n, t, &_s);
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
    if (ev & TQ_EV_RETO)
        _calibrado = NO;
    if ((ev & TQ_EV_MODO) && _n.modo == TQ_MODO_PILOTAGEM)
        _calibrado = NO; /* inclinação: o "reto" é como o iPhone está quando começa a pilotar */
    if (ev & (TQ_EV_LAYOUT | TQ_EV_MODO))
        [_vista refazer];
    else
        [_vista atualizarApertos];
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
            if (_visivel == TQVisivelEngrenagem && tq_controle_em(&_n, (float)p.x, (float)p.y) != TQ_C_AJUSTES)
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
    const float angulo = (float)(atan2(dm.gravity.y, dm.gravity.x) * (180.0 / 3.14159265358979323846));
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
    if (_vibracao == TQVibracaoTudo && _visivel == TQVisivelTudo) {
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
    NSData *mapa = [d dataForKey:K_MAPA];
    if (mapa.length == TQ_ACAO_COUNT)
        memcpy(aj.mapa, mapa.bytes, TQ_ACAO_COUNT);
    tq_definir_ajustes(&_n, &aj); /* corrige o que vier estragado */
    _transparencia = [d objectForKey:K_TRANSPARENCIA] ? [d floatForKey:K_TRANSPARENCIA] : 0.7f;
    if (!(_transparencia >= 0.3f && _transparencia <= 1.0f))
        _transparencia = 0.7f;
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
    [d setObject:[NSData dataWithBytes:_n.aj.mapa length:TQ_ACAO_COUNT] forKey:K_MAPA];
    [d setFloat:_transparencia forKey:K_TRANSPARENCIA];
    [d setInteger:_vibracao forKey:K_VIBRACAO];
}

/* muda um ajuste do núcleo, salva, redesenha e volta para a folha de ajustes */
- (void)mudar:(void (^)(tq_ajustes *aj))mudanca
{
    tq_ajustes aj = _n.aj;
    mudanca(&aj);
    tq_soltar_tudo(&_n);
    tq_definir_ajustes(&_n, &aj);
    _vista.transparencia = _transparencia;
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
    if (!_vc)
        return;
    tq_soltar_tudo(&_n);
    __weak TQCamada *fraca = self;
    NSString *msg = [NSString stringWithFormat:@"Controles por toque, build %s.\nMapa, SMS e as marchas ainda vão ser "
                                               @"confirmados no jogo: se algum não funcionar, use Reatribuir botões.",
                                               TQ_BUILD];
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Toque"
                                                               message:msg
                                                        preferredStyle:UIAlertControllerStyleActionSheet];
    [a addAction:[self acao:[@"Direção: " stringByAppendingString:nomeDirecao(_n.aj.direcao)]
                        faz:^{
                            [fraca escolherDirecao];
                        }]];
    [a addAction:[self acao:[@"Tamanho: " stringByAppendingString:porcento(_n.aj.escala)]
                        faz:^{
                            [fraca escolherTamanho];
                        }]];
    [a addAction:[self acao:[@"Transparência: " stringByAppendingString:porcento(_transparencia)]
                        faz:^{
                            [fraca escolherTransparencia];
                        }]];
    [a addAction:[self acao:[@"Vibração: " stringByAppendingString:nomeVibracao(_vibracao)]
                        faz:^{
                            [fraca escolherVibracao];
                        }]];
    [a addAction:[self acao:_n.aj.cambio_manual ? @"Câmbio manual (M+ e M−): sim" : @"Câmbio manual (M+ e M−): não"
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
                         message:@"Inclinar: vire o iPhone como um volante. O jeito que ele estiver quando você começar "
                                 @"a pilotar conta como reto (o botão RETO acerta de novo)."
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

- (void)escolherTamanho
{
    __weak TQCamada *fraca = self;
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Tamanho dos controles"
                                                               message:nil
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

- (void)escolherTransparencia
{
    __weak TQCamada *fraca = self;
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Transparência"
                                                               message:@"100% é o mais visível."
                                                        preferredStyle:UIAlertControllerStyleActionSheet];
    for (int i = 0; i < N_TRANSPARENCIAS; i++) {
        const float v = TRANSPARENCIAS[i];
        NSString *t = fabsf(v - _transparencia) < 0.01f ? [@"✓ " stringByAppendingString:porcento(v)] : porcento(v);
        [a addAction:[self acao:t
                            faz:^{
                                [fraca definirTransparencia:v];
                            }]];
    }
    [a addAction:[self voltar]];
    [self mostrar:a];
}

- (void)definirTransparencia:(float)v
{
    _transparencia = v;
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
    UIAlertController *a = [UIAlertController alertControllerWithTitle:texto(tq_nome_acao(acao))
                                                               message:@"Qual botão do controle essa ação aperta?"
                                                        preferredStyle:UIAlertControllerStyleActionSheet];
    for (int saida = 0; saida < TQ_O_COUNT; saida++) {
        NSString *nome = texto(tq_nome_saida(saida));
        NSString *t = saida == _n.aj.mapa[acao] ? [@"✓ " stringByAppendingString:nome] : nome;
        [a addAction:[self acao:t
                            faz:^{
                                [fraca mudar:^(tq_ajustes *aj) {
                                    aj->mapa[acao] = (uint8_t)saida;
                                }];
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
                                                                       @"padrão, inclusive os botões reatribuídos."
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
    for (NSString *k in @[ K_DIRECAO, K_ESCALA, K_TRANSPARENCIA, K_VIBRACAO, K_CAMBIO, K_PAUSA, K_MAPA ])
        [d removeObjectForKey:k];
    [self lerAjustes];
    _vista.transparencia = _transparencia;
    [self pararVibracao];
    [_vista refazer];
    [self abrirAjustes];
}

/* ----- diagnóstico ----- */

- (NSString *)textoDiagnostico
{
    char ponte[700];
    ps_status_texto(ponte, sizeof ponte);
    NSMutableString *t = [NSMutableString stringWithFormat:@"Toque build %s\n%@\n", TQ_BUILD, texto(ponte)];
    const double livre = (double)os_proc_available_memory() / (1024.0 * 1024 * 1024);
    const double total = (double)NSProcessInfo.processInfo.physicalMemory / (1024.0 * 1024 * 1024);
    [t appendFormat:@"Memória que o app ainda pode usar: %.2f GB (o iPhone tem %.1f GB)\n", livre, total];
    static NSString *const nomesVisivel[] = { @"escondida", @"só a engrenagem", @"todos os controles" };
    [t appendFormat:@"Camada: %@%@\n", _visivel >= 0 && _visivel <= 2 ? nomesVisivel[_visivel] : @"?",
                    _forcar ? @" (sempre à mostra)" : @""];
    [t appendFormat:@"Modo: %@. Direção: %@. Tamanho: %@\n", _n.modo == TQ_MODO_PILOTAGEM ? @"Pilotar" : @"Menu",
                    nomeDirecao(_n.aj.direcao), porcento(_n.aj.escala)];
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
