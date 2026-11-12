#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <string>
#include <windows.h>

using namespace std;

// ===================== CONST ========================
const int CICLO_MS = 33;      // ~30 FPS (ritmo do jogo)
const int DELAY_MS = 150;     // delay minimo entre passos de cada jogador
const int KABOOM_MS = 3000;   // pavio da bomba (3 segundos)
const int FOGO_MS = 500;      // tempo que o fogo permanece ativo
const int ROUND_MS = 120000;  // duracao maxima do round (2 minutos)
const int ALCANCE_PADRAO = 2; // alcance padrao da explosao
const int MAX_BOMBAS = 3;     // maximo de bombas ativas por jogador

// ===================== CONSTANTES MAPA =====================
const char PAREDE = '#';
const char BLOCO = '+';
const char VAZIO = '.';
const char BOMBA = 'B';
const char FOGO = '*';
const char P1 = '1';
const char P2 = '2';

// ===================== STRUCTS =====================

struct Mapa
{
	int l, c;
	int linhaInicial[2], colunaInicial[2];
	string titulo;
	char **grid; // matriz dinamica
};

struct Bomba
{
	int l, c;
	int dono;	      // 0 (Player 1) ou 1 (Player 2)
	int range;	      // default 2
	long long colocadaEm; // momento plantada
};

struct Chama
{
	int l, c;
	long long colocadaEm; // momento fogo
};

struct Player
{
	string nome;
	int l, c;
	bool vivo;
	int vitorias;
	long long proximoPassoEm; // momento prox passo
};

struct Game
{
	Mapa mapa;
	int mapaId;
	Player p[2];

	Bomba *bombas;
	int bombasAtivas, bombaCap;

	Chama *chamas;
	int chamasAtivas, chamaCap;

	int round;	       // 1 a 3
	long long comecoRound; // ms em que o round atual comecou
	long long gameClock;   // tempo acumulado em ms (sem contar pausas)
	bool pausado;
	long long ultimoUpdate; // ultima marcacao no tempo real
};

struct RegistroHistorico
{
	string nome1, nome2;
	int mapaId, vitorias1, vitorias2, segundos;
};

// ============================================================================
// PROTOTÓTIPOS
// ============================================================================

// --- Templates ---
template <typename T>
void cresceVetor(T *&vetor, int &cap);
template <typename T>
void removeDoVetor(T *vetor, int &qtd, int index);
template <typename T>
bool leCelulas(ifstream &f, Mapa &m, string &erro);

// --- Alloc ---
char **criaMatriz(int l, int c);
void liberaMatriz(char **m, int l);

// --- Tempo ---
long long agoraMs();
void atualizaRelogio(Game &g);
void alternaPausa(Game &g);

// --- Mapa ---
int contaMapas();
bool carregaMapa(int id, Mapa &m, string &erro);
void liberaMapa(Mapa &m);

// --- Historico ---
void salvaHistorico(const Game &g);
RegistroHistorico *carregaHistorico(int &total);
void exibeHistorico(RegistroHistorico *h, int total);

// --- Movimento e Colisao ---
bool podeAndar(const Game &g, int idPlayer, int nl, int nc);
void movePlayer(Game &g, int idPlayer, int dl, int dc);

// --- Bombas e Chamas ---
int contaBombas(const Game &g, int idPlayer);
void colocaBomba(Game &g, int idPlayer);
void adicionaChama(Game &g, int l, int c);
void explodeBomba(Game &g, int indexBomba);
void atualizaBombas(Game &g);
void atualizaChamas(Game &g);
void verificaDanoFogo(Game &g);

// --- Input ---
void leTeclas(Game &g);

// --- Desenho ---
void posicionaCursor(int x, int y);
void desenhaHUD(const Game &g);
void desenha(const Game &g);

// --- Controle do Jogo / Rodada ---
void iniciaRound(Game &g);
void finalizaRound(Game &g);
bool verificaFimRound(const Game &g);
void executaPartida(Game *g);

// --- Menus ---
int exibeMenu();
void menuJogar();
void menuHistorico();
void menuComoJogar();
void menuSobre();

// ===================== ALLOC (TEMPLATES) ============

// Dobra a capacidade do vetor quando estiver cheio (Bombas e Chamas)
template <typename T>
void cresceVetor(T *&vetor, int &cap)
{
	int novaCap = 0;
	if (cap == 0)
	{
		novaCap = 2;
	}
	else
	{
		novaCap = cap * 2;
	}
	T *novoVetor = new T[novaCap]; // cria novo

	for (int i = 0; i < cap; i++) // copia
	{
		novoVetor[i] = vetor[i];
	}

	delete[] vetor;	   // expurga
	vetor = novoVetor; // muda pra onde aponta
	cap = novaCap;
}

// Remove elemento do vetor trocando com o ultimo elemento
template <typename T>
void removeDoVetor(T *vetor, int &qtd, int index)
{
	vetor[index] = vetor[qtd - 1];
	qtd--;
}

char **criaMatriz(int l, int c)
{
	char **m = new char *[l];
	for (int i = 0; i < l; i++)
	{
		m[i] = new char[c];
	}
	return m;
}

void liberaMatriz(char **m, int l)
{
	if (m == nullptr)
		return;
	for (int i = 0; i < l; i++)
	{
		delete[] m[i];
	}
	delete[] m;
}

// ===================== TEMPO ========================

// Retorna steady_clock em ms
long long agoraMs()
{
	using namespace chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(); // usa epoch pra evitar problema com a pausa dps
}

// Se n pausou, atualiza relógio
void atualizaRelogio(Game &g)
{
	long long agora = agoraMs();
	if (!g.pausado)
	{
		g.gameClock += (agora - g.ultimoUpdate); // fica 0 quando despausa
	}
	g.ultimoUpdate = agora;
}

// Alterna a pausa
void alternaPausa(Game &g)
{
	g.pausado = !g.pausado;
	g.ultimoUpdate = agoraMs();
}

// ===================== MAPA =======================

// Tenta abrir mapa(id).txt até n encontra mais
int contaMapas()
{
	int total = 0;
	while (true)
	{
		string nome = "mapa" + to_string(total + 1) + ".txt";
		ifstream f(nome);
		if (!f.is_open())
			break; // aq para
		total++;
	}
	return total;
}

char converteParaChar(char c)
{
	if (c == PAREDE || c == BLOCO || c == VAZIO || c == P1 || c == P2)
	{
		return c;
	}
	return '?'; // caractere inválido
}

char converteParaChar(int n)
{
	switch (n)
	{
	case 0:
		return VAZIO; // vazio
	case 1:
		return PAREDE; // parede
	case 2:
		return BLOCO; // bloco destrutível
	case 3:
		return P1; // Jogador 1
	case 4:
		return P2; // Jogador 2
	default:
		return '?'; // numero invalido
	}
}

// Template pra ler Char e Int
template <typename T>
bool leCelulas(ifstream &f, Mapa &m, string &erro)
{
	// TODO: ler valores da arena e converter para char interno ('#', '+', '.', etc.)
	int qtdP1 = 0, qtdP2 = 0;

	for (int i = 0; i < m.l; i++)
	{
		for (int j = 0; j < m.c; j++)
		{
			T valor;
			if (!(f >> valor))
			{
				erro = "Faltam celulas";
				liberaMatriz(m.grid, m.l);
				m.grid = nullptr;
				return false;
			}

			char c = converteParaChar(valor);
			if (c == '?')
			{
				erro = "Celula invalida";
				liberaMatriz(m.grid, m.l);
				m.grid = nullptr;
				return false;
			}

			//  P1
			if (c == P1)
			{
				m.linhaInicial[0] = i;
				m.colunaInicial[0] = j;
				qtdP1++;
				c = VAZIO; // celula vazia no mapa vira .
			}
			// P2
			else if (c == P2)
			{
				m.linhaInicial[1] = i;
				m.colunaInicial[1] = j;
				qtdP2++;
				c = VAZIO;
			}
			m.grid[i][j] = c;
		}
	}

	if (qtdP1 != 1 || qtdP2 != 1)
	{
		erro = "Mapa deve ter exatamente um P1 e um P2";
		liberaMatriz(m.grid, m.l);
		m.grid = nullptr;
		return false;
	}

	return true;
}

// Abre mapa{id}.txt, aloca memoria e valida dimensoes e jogadores
bool carregaMapa(int id, Mapa &m, string &erro)
{
	ifstream f("mapa" + to_string(id) + ".txt");
	if (!f.is_open())
	{
		erro = "Arquivo nao encontrado";
		liberaMatriz(m.grid, m.l);
		m.grid = nullptr;
		return false;
	}

	char formato;
	f >> m.l >> m.c >> formato;

	string come;
	getline(f, come); // resto da linha
	getline(f, m.titulo);

	if (!(m.l >= 5 && m.c >= 5))
	{
		erro = "tamanho invalido";
		liberaMatriz(m.grid, m.l);
		m.grid = nullptr;
		return false;
	}

	if (!(formato == 'C' || formato == 'c' || formato == 'N' || formato == 'n'))
	{
		erro = "formato invalido";
		liberaMatriz(m.grid, m.l);
		m.grid = nullptr;
		return false;
	}

	m.grid = criaMatriz(m.l, m.c);

	if (formato == 'C' || formato == 'c')
	{
		return leCelulas<char>(f, m, erro);
	}
	else if (formato == 'N' || formato == 'n')
	{
		return leCelulas<int>(f, m, erro);
	}
	else
	{
		erro = "Formato desconhecido";
		liberaMapa(m);
		return false;
	}

	return true;
}

// Libera memória
void liberaMapa(Mapa &m)
{
	liberaMatriz(m.grid, m.l);
	m.grid = nullptr;
}

// ===================== HISTORICO =======================

// Salva linha no formato: nome1,nome2,mapa,vitorias1,vitorias2,segundos
void salvaHistorico(const Game &g)
{
	// TODO: gravar dados ao final de historico.csv
}

// Conta linhas, aloca vetor exato e preenche os registros
RegistroHistorico *carregaHistorico(int &total)
{
	total = 0;
	// TODO: ler arquivo historico.csv se existir
	return nullptr;
}

// Mostra o total de partidas e as 10 mais recentes
void exibeHistorico(RegistroHistorico *h, int total)
{
	// TODO: imprimir tabela de partidas
}

// ===================== MOVIMENTO ====================

// Checa colisoes com paredes, blocos, bombas ou o outro jogador
bool podeAndar(const Game &g, int idPlayer, int nl, int nc)
{
	// Retorna false se (nl, nc) for parede (#), bloco (+), tiver bomba ativa ou for a posição atual do outro jogador.
	if (nl < 0 || nl >= g.mapa.l || nc < 0 || nc >= g.mapa.c)
		return false; // fora do mapa

	char celula = g.mapa.grid[nl][nc];

	if (celula == PAREDE || celula == BLOCO)
		return false; // parede ou bloco

	for (int i = 0; i < g.bombasAtivas; i++)
	{
		if (g.bombas[i].l == nl && g.bombas[i].c == nc)
		{
			return false; // bomba ativa
		}
	}

	int outroPlayer = (idPlayer == 0) ? 1 : 0; // jogador oposto

	if (g.p[outroPlayer].vivo && g.p[outroPlayer].l == nl && g.p[outroPlayer].c == nc)
	{
		return false; // outro jogador
	}

	return true;
}

// Movo player, verifica delay 150ms e colisão
void movePlayer(Game &g, int idPlayer, int dl, int dc)
{
	// TODO: checar g.gameClock >= p[idPlayer].proximoPassoEm e chamar podeAndar
	int nl = g.p[idPlayer].l + dl;
	int nc = g.p[idPlayer].c + dc;
	if (g.gameClock >= g.p[idPlayer].proximoPassoEm && podeAndar(g, idPlayer, nl, nc))
	{
		g.p[idPlayer].l += dl;
		g.p[idPlayer].c += dc;
		g.p[idPlayer].proximoPassoEm = g.gameClock + DELAY_MS; // atualiza o delay de 150ms
	}
}

// ===================== BOMBAS =======================

// Conta bombas do player 1 ou 2
int contaBombas(const Game &g, int idPlayer)
{
	int total = 0;
	for (int i = 0; i < g.bombasAtivas; i++)
	{
		if (g.bombas[i].dono == idPlayer)
			total++;
	}
	return total;
}

// Planta bomba se o jogador tiver menos de 3 ativas e celula vazia
void colocaBomba(Game &g, int idPlayer)
{
	char celula = g.mapa.grid[g.p[idPlayer].l][g.p[idPlayer].c];
	if (contaBombas(g, idPlayer) < 3 && celula == VAZIO)
	{
		if (g.bombasAtivas >= g.bombaCap)
		{
			cresceVetor(g.bombas, g.bombaCap);
		}
		g.bombas[g.bombasAtivas].l = g.p[idPlayer].l;
		g.bombas[g.bombasAtivas].c = g.p[idPlayer].c;
		g.bombas[g.bombasAtivas].dono = idPlayer;
		g.bombas[g.bombasAtivas].range = ALCANCE_PADRAO;
		g.bombas[g.bombasAtivas].colocadaEm = g.gameClock;
		g.bombasAtivas++;
	}
	// TODO: cresceVetor se necessario e registra bomba
}

// Adiciona uma chama ao vetor dinamico de chamas
void adicionaChama(Game &g, int l, int c)
{
	// TODO: cresceVetor se necessario e insere no vetor
	if (g.chamasAtivas >= g.chamaCap)
	{
		cresceVetor(g.chamas, g.chamaCap);
	}
	g.chamas[g.chamasAtivas].l = l;
	g.chamas[g.chamasAtivas].c = c;
	g.chamas[g.chamasAtivas].colocadaEm = g.gameClock;
	g.chamasAtivas++;
}

// Explode bomba em cruz, quebra blocos, cria chamas e gera reacao em cadeia
void explodeBomba(Game &g, int indexBomba)
{
	// TODO: propagar chamas nas 4 direcoes e remover bomba com removeDoVetor
	int l = g.bombas[indexBomba].l;
	int c = g.bombas[indexBomba].c;
	removeDoVetor(g.bombas, g.bombasAtivas, indexBomba);

	// chama do centro
	adicionaChama(g, l, c);
	// Cima, Baixo, Esquerda, Direita
	int dL[4] = {-1, 1, 0, 0};
	int dC[4] = {0, 0, -1, 1};

	for (int i = 0; i < 4; i++)
	{
		for (int r = 1; r <= ALCANCE_PADRAO; r++)
		{
			int nl = l + (dL[i] * r);
			int nc = c + (dC[i] * r);

			if (nl < 0 || nl >= g.mapa.l || nc < 0 || nc >= g.mapa.c)
			{
				break; // fora do mapa
			}

			char celula = g.mapa.grid[nl][nc];

			if (celula == PAREDE)
				break; // indestrutível


			if (celula == BLOCO)
			{
				g.mapa.grid[nl][nc] = VAZIO; // destrói bloco
				adicionaChama(g, nl, nc); 
				break; // chama para ali				
			}

			if (celula == VAZIO)
			{
				adicionaChama(g, nl, nc); // normal
			}

			for (int b = 0; b < g.bombasAtivas; b++)
			{
				if (g.bombas[b].l == nl && g.bombas[b].c == nc)
				{
					explodeBomba(g, b); // reação em cadeia
					break;
				}
			}

		}
	}
}

// KABOOM
void atualizaBombas(Game &g)
{
	// TODO: verificar pavios vencidos
	for (int i = 0; i < g.bombasAtivas; i++)
	{
		if (g.gameClock - g.bombas[i].colocadaEm >= KABOOM_MS)
		{
			explodeBomba(g, i);
			i--; // a bomba remove do vetor
		}
	}
}

// Remove chamas que passaram de FOGO_MS
void atualizaChamas(Game &g)
{
	// TODO: verificar chamas expiradas
	for (int i = 0; i < g.chamasAtivas; i++)
	{
		if (g.gameClock - g.chamas[i].colocadaEm >= FOGO_MS)
		{
			removeDoVetor(g.chamas, g.chamasAtivas, i);
			i--; // a chama remove do vetor
		}
	}
}

void verificaDanoFogo(Game &g)
{
	// Player vivo = false se tiver na chama
	for (int i = 0; i < g.chamasAtivas; i++)
	{
		for (int p = 0; p < 2; p++)
		{
			if (g.p[p].vivo && g.p[p].l == g.chamas[i].l && g.p[p].c == g.chamas[i].c)
			{
				g.p[p].vivo = false;
			}
		}
	}
}

// ===================== INPUT =======================

// Le as teclas simultaneamente via GetAsyncKeyState (sem getch)
void leTeclas(Game &g)
{
	// P1: W, A, S, D, F (bomba)
	// P2: Setinhas, Enter (bomba)
	// P: Pausa / Despausa
	// ESC: Sair

	static bool teclaP_EstavaPressionada = false; // GAMBIARRA INSANA.

	// p1
	if (GetAsyncKeyState('W') & 0x8000)
	{
		movePlayer(g, 0, -1, 0); // Move para cima
	}
	else if (GetAsyncKeyState('A') & 0x8000)
	{
		movePlayer(g, 0, 0, -1); // Move para a esquerda
	}
	else if (GetAsyncKeyState('S') & 0x8000)
	{
		movePlayer(g, 0, 1, 0); // Move para baixo
	}
	else if (GetAsyncKeyState('D') & 0x8000)
	{
		movePlayer(g, 0, 0, 1); // Move para a direita
	}
	else if (GetAsyncKeyState('F') & 0x8000)
	{
		colocaBomba(g, 0);
	}

	// p2
	if (GetAsyncKeyState(VK_UP) & 0x8000)
	{
		movePlayer(g, 1, -1, 0); // Move para cima
	}
	else if (GetAsyncKeyState(VK_DOWN) & 0x8000)
	{
		movePlayer(g, 1, 1, 0); // Move para baixo
	}
	else if (GetAsyncKeyState(VK_LEFT) & 0x8000)
	{
		movePlayer(g, 1, 0, -1); // Move para a esquerda
	}
	else if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
	{
		movePlayer(g, 1, 0, 1); // Move para a direita
	}
	else if (GetAsyncKeyState(VK_RETURN) & 0x8000)
	{
		colocaBomba(g, 1);
	}

	// Pausa e Sair
	if ((GetAsyncKeyState('P') & 0x8000) && !teclaP_EstavaPressionada)
	{
		alternaPausa(g);
		teclaP_EstavaPressionada = true;
	}
	else
	{
		teclaP_EstavaPressionada = false;
	}

	if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
	{
		g.p[0].vivo = false;
		g.p[1].vivo = false;
	}
}

// ===================== DESENHO =====================

// Move o cursor sem limpar a tela inteira (evita flicker / piscar)
void posicionaCursor(int x, int y)
{
	COORD c;
	c.X = (SHORT)x;
	c.Y = (SHORT)y;
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// Mostra placar, nomes, bombas disponiveis e tempo restante mm:ss
void desenhaHUD(const Game &g)
{
	long long tempoDecorrido = g.gameClock - g.comecoRound;
	long long tempoRestanteMs = ROUND_MS - tempoDecorrido;
	if (tempoRestanteMs < 0)
		tempoRestanteMs = 0;

	int totalSegundos = tempoRestanteMs / 1000;
	int min = totalSegundos / 60;
	int seg = totalSegundos % 60;

	int bombasP1 = MAX_BOMBAS - contaBombas(g, 0);
	int bombasP2 = MAX_BOMBAS - contaBombas(g, 1);

	cout << "======================================================" << endl;
	cout << "  RODADA: " << g.round << "/3   |   TEMPO RESTANTE: ";
	if (min < 10)
		cout << "0";
	cout << min << ":";
	if (seg < 10)
		cout << "0";
	cout << seg << endl;
	cout << "======================================================" << endl;
	cout << "  [P1] " << g.p[0].nome << " - Vitorias: " << g.p[0].vitorias
	     << " | Bombas: " << bombasP1 << "/" << MAX_BOMBAS << endl;
	cout << "  [P2] " << g.p[1].nome << " - Vitorias: " << g.p[1].vitorias
	     << " | Bombas: " << bombasP2 << "/" << MAX_BOMBAS << endl;
	cout << "======================================================" << endl;
}

// Desenha o mapa, jogadores, bombas e chamas
void desenha(const Game &g)
{
	posicionaCursor(0, 0);
	desenhaHUD(g);
	for (int i = 0; i < g.mapa.l; i++)
	{
		for (int j = 0; j < g.mapa.c; j++)
		{
			char c = g.mapa.grid[i][j];

			// Logica bomba
			bool bombaPresente = false;
			for (int b = 0; b < g.bombasAtivas; b++)
			{
				if (g.bombas[b].l == i && g.bombas[b].c == j)
				{
					bombaPresente = true;
					break;
				}
			}

			// Chama
			bool chamaPresente = false;
			for (int ch = 0; ch < g.chamasAtivas; ch++)
			{
				if (g.chamas[ch].l == i && g.chamas[ch].c == j)
				{
					chamaPresente = true;
					break;
				}
			}

			// Jogador
			for (int p = 0; p < 2; p++)
			{
				if (g.p[p].vivo && g.p[p].l == i && g.p[p].c == j)
				{
					c = (p == 0) ? P1 : P2; // Player 1 ou 2
					break;
				}
			}

			// Chama > Bomba > Jogador / Mapa
			if (chamaPresente)
				cout << FOGO; // Chama
			else if (bombaPresente)
				cout << BOMBA; // Bomba
			else
				cout << c; // Jogador ou Mapa
		}
		cout << endl;
	}
}

// ===================== CONTROLE DO JOGO =============

// Prepara o round restaurando o mapa original e posicoes
void iniciaRound(Game &g)
{
	// TODO: recarregar mapa, reposicionar players, zerar bombas/chamas
}

// Libera memoria dinamica de bombas e chamas ao fim do round
void finalizaRound(Game &g)
{
	// TODO: desalocar vetores de bombas e chamas
}

// Condicao de parada: <= 1 jogador vivo ou tempo de 2 min esgotado
bool verificaFimRound(const Game &g)
{
	// TODO: checar vidas e cronometro do round
	return false;
}

// Laco principal da partida: recebe Game por ponteiro (requisito do PDF com ->)
void executaPartida(Game *g)
{
	g->ultimoUpdate = agoraMs();

	while (true)
	{
		long long inicioCiclo = agoraMs();

		atualizaRelogio(*g);
		leTeclas(*g);

		if (!g->pausado)
		{
			atualizaBombas(*g);
			atualizaChamas(*g);
			verificaDanoFogo(*g);

			if (verificaFimRound(*g))
			{
				break;
			}
		}

		desenha(*g);

		long long tempoCiclo = agoraMs() - inicioCiclo;
		if (tempoCiclo < CICLO_MS)
		{
			this_thread::sleep_for(chrono::milliseconds(CICLO_MS - tempoCiclo));
		}
	}
}

// ===================== MENU ========================

int exibeMenu()
{
	// TODO: desenhar menu e ler opcao escolhida
	return 5;
}

void menuJogar()
{
	// TODO: selecao de mapas, nomes dos jogadores e loop das rodadas
}

void menuHistorico()
{
	// TODO: carregar historico, exibir e liberar vetor
}

void menuComoJogar()
{
	// TODO: imprimir instrucoes e comandos
}

void menuSobre()
{
	// TODO: nomes do trio, disciplina, professor e ano
}

// ===================== MAIN ========================

int main()
{
	// Oculta o cursor piscante do terminal
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_CURSOR_INFO cursor;
	GetConsoleCursorInfo(out, &cursor);
	cursor.bVisible = FALSE;
	SetConsoleCursorInfo(out, &cursor);

	/*
	int opcao = 0;
	do
	{
	    opcao = exibeMenu();
	    switch (opcao)
	    {
	    case 1:
		menuJogar();
		break;
	    case 2:
		menuHistorico();
		break;
	    case 3:
		menuComoJogar();
		break;
	    case 4:
		menuSobre();
		break;
	    case 5:
		cout << "\nSaindo do Bomberman...\n";
		break;
	    default:
		cout << "\nOpcao invalida!\n";
		break;
	    }
	} while (opcao != 5);*/

	system("cls");
	Game g;
	string erro;
	if (!carregaMapa(1, g.mapa, erro))
	{
		cout << "Erro ao carregar mapa: " << erro << endl;
		return 1;
	}
	// Inicializa valores de teste da partida
	g.round = 1;
	g.comecoRound = 0;
	g.gameClock = 0;
	g.pausado = false;
	g.bombas = nullptr;
	g.bombasAtivas = 0;
	g.bombaCap = 0;
	g.chamas = nullptr;
	g.chamasAtivas = 0;
	g.chamaCap = 0;
	// Jogadores posicionados nas coordenadas lidas do arquivo
	g.p[0] = {"Ana", g.mapa.linhaInicial[0], g.mapa.colunaInicial[0], true, 0, 0};
	g.p[1] = {"Bruno", g.mapa.linhaInicial[1], g.mapa.colunaInicial[1], true, 0, 0};
	// --- TESTE 1: Desenho estático simples ---
	executaPartida(&g);
	liberaMapa(g.mapa);

	return 0;
}