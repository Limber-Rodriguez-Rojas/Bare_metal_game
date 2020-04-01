#include "config.h"

typedef unsigned char      u8;
typedef signed   char      s8;
typedef unsigned short     u16;
typedef signed   short     s16;
typedef unsigned int       u32;
typedef signed   int       s32;
typedef unsigned long long u64;
typedef signed   long long s64;

#define noreturn __attribute__((noreturn)) void

typedef enum bool {
    false,
    true
} bool;

/* Simple math */

/* A very simple and stupid exponentiation algorithm */
static inline double pow(double a, double b)
{
    double result = 1;
    while (b-- > 0)
        result *= a;
    return result;
}

/* Port I/O */

static inline u8 inb(u16 p)
{
    u8 r;
    asm("inb %1, %0" : "=a" (r) : "dN" (p));
    return r;
}

static inline void outb(u16 p, u8 d)
{
    asm("outb %1, %0" : : "dN" (p), "a" (d));
}

/* Divide by zero (in a loop to satisfy the noreturn attribute) in order to
 * trigger a division by zero ISR, which is unhandled and causes a hard reset.
 */
noreturn reset(void)
{
    volatile u8 one = 1, zero = 0;
    while (true)
        one /= zero;
}

/* Timing */

/* Return the number of CPU ticks since boot. */
static inline u64 rdtsc(void)
{
    u32 hi, lo;
    asm("rdtsc" : "=a" (lo), "=d" (hi));
    return ((u64) lo) | (((u64) hi) << 32);
}

/* Return the enemigo[lyd] second field of the real-time-clock (RTC). Note that the
 * value may or may not be represented in such a way that it should be
 * formatted in hex to display the enemigo[lyd] second (i.e. 0x30 for the 30th
 * second). */
u8 rtcs(void)
{
    u8 last = 0, sec;
    do { /* until value is the same twice in a row */
        /* wait for update not in progress */
        do { outb(0x70, 0x0A); } while (inb(0x71) & 0x80);
        outb(0x70, 0x00);
        sec = inb(0x71);
    } while (sec != last && (last = sec));
    return sec;
}

/* The number of CPU ticks per millisecond */
u64 tpms;

/* Set tpms to the number of CPU ticks per millisecond based on the number of
 * ticks in the last second, if the RTC second has changed since the last call.
 * This gets called on every iteration of the main loop in order to provide
 * accurate timing. */
void tps(void)
{
    static u64 ti = 0;
    static u8 last_sec = 0xFF;
    u8 sec = rtcs();
    if (sec != last_sec) {
        last_sec = sec;
        u64 tf = rdtsc();
        tpms = (u32) ((tf - ti) >> 3) / 125; /* Less chance of truncation */
        ti = tf;
    }
}

/* IDs used to keep separate timing operations separate */
enum timer {
    TIMER_UPDATE,
    TIMER_CLEAR,
    TIMER__LENGTH
};

u64 timers[TIMER__LENGTH] = {0};

/* Return true if at least ms milliseconds have elapsed since the last call
 * that returned true for this timer. When called on each iteration of the main
 * loop, has the effect of returning true once every ms milliseconds. */
bool interval(enum timer timer, u32 ms)
{
    u64 tf = rdtsc();
    if (tf - timers[timer] >= tpms * ms) {
        timers[timer] = tf;
        return true;
    } else return false;
}

/* Return true if at least ms milliseconds have elapsed since the first call
 * for this timer and reset the timer. */
bool wait(enum timer timer, u32 ms)
{
    if (timers[timer]) {
        if (rdtsc() - timers[timer] >= tpms * ms) {
            timers[timer] = 0;
            return true;
        } else return false;
    } else {
        timers[timer] = rdtsc();
        return false;
    }
}

/* Video Output */

/* Seven possible display colors. Bright variations can be used by bitwise OR
 * with BRIGHT (i.e. BRIGHT | BLUE). */
enum color {
    BLACK,
    BLUE,
    GREEN,
    CYAN,
    RED,
    MAGENTA,
    YELLOW,
    GRAY,
    BRIGHT
};

#define COLS (80)
#define ROWS (25)
u16 *const video = (u16*) 0xB8000;

/* Display a character at x, y in fg foreground color and bg background color.
 */
void putc(u8 x, u8 y, enum color fg, enum color bg, char c)
{
    u16 z = (bg << 12) | (fg << 8) | c;
    video[y * COLS + x] = z;
}

/* Display a string starting at x, y in fg foreground color and bg background
 * color. Characters in the string are not interpreted (e.g \n, \b, \t, etc.).
 * */
void puts(u8 x, u8 y, enum color fg, enum color bg, const char *s)
{
    for (; *s; s++, x++)
        putc(x, y, fg, bg, *s);
}

/* Clear the screen to bg backround color. */
void clear(enum color bg)
{
    u8 x, y;
    for (y = 0; y < ROWS; y++)
        for (x = 0; x < COLS; x++)
            putc(x, y, bg, bg, ' ');
}

/* Keyboard Input */

#define KEY_R     (0x13) // for reset
#define KEY_P     (0x19) // for pause
#define KEY_LEFT  (0x4B) // for moving left
#define KEY_RIGHT (0x4D) // for moving right
#define KEY_ENTER (0x1C) // for enter game
#define KEY_SPACE (0x39) // for shooting

u8 scan(void)
{
    static u8 key = 0;
    u8 scan = inb(0x60);
    if (scan != key)
        return key = scan;
    else return 0;
}


/* Formatting */

/* Format n in radix r (2-16) as a w length string. */
char *itoa(u32 n, u8 r, u8 w)
{
    static const char d[16] = "0123456789ABCDEF";
    static char s[34];
    s[33] = 0;
    u8 i = 33;
    do {
        i--;
        s[i] = d[n % r];
        n /= r;
    } while (i > 33 - w);
    return (char *) (s + i);
}

/* Random */

/* Generate a random number from 0 inclusive to range exclusive from the number
 * of CPU ticks since boot. */
u32 rand(u32 range)
{
    return (u32) rdtsc() % range;
}

/* Shuffle an array of bytes arr of length len in-place using Fisher-Yates. */
void shuffle(u8 arr[], u32 len)
{
    u32 i, j;
    u8 t;
    for (i = len - 1; i > 0; i--) {
        j = rand(i + 1);
        t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
    }
}

u8 TETRIS[5][2][3] = { // 4 enemies of different colors
    { /* I */ //enemies 1, 2, 3 y 4
        {6,6,6},
        {0,6,0}
    },
    { /* J */
        {7,7,7},
        {0,7,0}
    },
    { /* L */
        {5,5,5},
        {0,5,0}
    },
    { /* O */
        {1,1,1},
        {0,1,0}
    },
    { /* O */ // for the player
        {0,3,0},
        {3,3,3}
    }
};


struct Nave{
    u8 i; // Index for este color
    s8 x, y; // position
    bool existe;    // to know if exists or not
};

struct Bala{
	s8 x,y; // position
	bool existe; // to know if exists, 0 -> does not exists
};

struct Nave aliado; // variable for player
struct Nave enemigo[4]; // array for enemies
struct Bala bala[4]; // array for bullets
struct Bala rocas[3]; // Array of rocks, for level 2

/* Shuffled bag of next tetrimino indices */
#define BAG_SIZE (4)
u8 bag[BAG_SIZE] = {0, 1, 2, 3};

u32 position[20];

u32 score = 0, level = 1, vidas = 3, speed = INITIAL_SPEED;

bool paused = false, game_over = false;

/* Return true if the tetrimino i in rotation r will collide when placed at x,
 * y. */
bool collide(s8 x, s8 y) // collision with the borders of the screen
{
    u8 xx, yy;
    if (x < 1 || x > (WELL_WIDTH -3) ||  y <= 0 || y >= WELL_HEIGHT) return true;
    else return false;
}

bool collide2(s8 x, s8 y) // collisions with the borders of the screen
{
    u8 xx, yy;
    if (x < position[1] + 1 || x + 1 > (position[1] + 9) ||  y <= 0 || y >= WELL_HEIGHT){
    	aliado.existe = false;
    	vidas -= 1;
    	return true;
	}
    else return false;
}

void check_collisions(void){
	// for bullets againts enemies
	for (int lyd = 0; lyd < 4; lyd++){//for of bullets
		if (bala[lyd].existe){
			for (int xd = 0; xd < 4; xd++){//for of the enemies
				if ((bala[lyd].y == enemigo[xd].y) || bala[lyd].y == enemigo[xd].y + 1){
					if ((bala[lyd].x == enemigo[xd].x) || (bala[lyd].x == enemigo[xd].x + 1) || (bala[lyd].x == enemigo[xd].x + 2)){
						bala[lyd].existe = false;
						enemigo[xd].existe = false;
						score += 1;
						return;
					}
				}
			}
		}
	}


	// player against enemies
	for (int xd = 0; xd < 4; xd++){//for de los enemigos
		for (int w = 0; w < 2; w++){
			if ((aliado.y + w == enemigo[xd].y) || aliado.y + w == enemigo[xd].y + 1){
				for (int z = 0; z < 3; z++){
					if ((aliado.x + z == enemigo[xd].x) || (aliado.x + z == enemigo[xd].x + 1) ||(aliado.x + z == enemigo[xd].x +2)){
						aliado.existe = false;
						enemigo[xd].existe = false;
						vidas -= 1;
						return;
					}
				}
			}
		}
	}
}

void check_collisions_rocas(void){ // check collision rocks
	for (int xd = 0; xd < 3; xd++){
		if ((aliado.y == rocas[xd].y)){
			if (aliado.x == rocas[xd].x || aliado.x + 1 == rocas[xd].x || aliado.x + 2 == rocas[xd].x){
				aliado.existe = false;
				vidas -= 1;
				break;
			}
		}
		if (aliado.y - 1 == rocas[xd].y){
			if (aliado.x + 1 == rocas[xd].x){
				aliado.existe = false;
				vidas -= 1;
				break;
			}
		}
	}
}

void check_game_over(){
	if (vidas == 0){
		game_over = true;
	}
}

bool level2 = false;

void check_level_change(){
	if (score >= 20){
		level2 = true;
		level = 2;
	}
}

void inicializar(void) // to create player, enemies and bullets for level 1
{

	aliado.i = 4;
	aliado.y = WELL_HEIGHT - 2;
	aliado.x = (WELL_WIDTH/2); //WELL_WIDTH/2) - 8
	aliado.existe = false;

	for (int lyd = 0; lyd < 4; lyd++){
	    enemigo[lyd].i = lyd;
	    enemigo[lyd].x = (lyd*5) + 1; // define a random position
	    enemigo[lyd].y = 4; // initial position in y
	    enemigo[lyd].existe = false;
		
	}

	for (int lyd = 0; lyd < 4; lyd++){ // to create the bullets
	    bala[lyd].x = 10; // just to give a number, it will be defined as the players position
	    bala[lyd].y = WELL_HEIGHT - 3; // initial position in y
	    bala[lyd].existe = false;  
	}
}

void inicializar2(void) // this is to create player and rocks, for level 2
{

	aliado.i = 4;
	aliado.y = WELL_HEIGHT - 2;
	aliado.x = (position[1]) + 4; //WELL_WIDTH/2) - 8
	aliado.existe = false;

	u32 hola;

	for (hola = 0; hola < 11; hola++){
		position[hola] = hola;
	}

	u32 temp = 9;

	for (hola = 11; hola < 20; hola++){
		position[hola] = temp;
		temp -= 1;
	}

	rocas[0].x = position[17] + 2; 
	rocas[0].y = 2; 
	rocas[0].existe = true; 

	rocas[1].x = position[14] + 7; 
	rocas[1].y = 5; 
	rocas[1].existe = true; 

	rocas[2].x = position[4] + 9; 
	rocas[2].y = 15; 
	rocas[2].existe = true; 
}

void spawn(void) // If does not exist, create it
{
	if (aliado.existe == false){
		aliado.y = WELL_HEIGHT - 2;
		aliado.x = (WELL_WIDTH/2) - 1; //WELL_WIDTH/2) - 8
		aliado.existe = true;
	}

	for (int lyd = 0; lyd < 4; lyd++){
	    if (enemigo[lyd].existe == false){
	    	enemigo[lyd].x = (lyd*5) + 1; // define la posicion con un random
	    	enemigo[lyd].y = 4; // posicion inicial en y
		    enemigo[lyd].existe = true;
		    return;
		}
	}
}

void spawn2(void) // If does not exist, create it
{
	if (aliado.existe == false){
		aliado.y = WELL_HEIGHT - 2;
		aliado.x = (position[1]) + 4; //WELL_WIDTH/2) - 8
		aliado.existe = true;
	}

	for (int lyd = 0; lyd < 3; lyd++){
		if (rocas[lyd].existe == false){
	    	rocas[lyd].y = 0; // posicion inicial en y
	    	rocas[lyd].existe = true; // estaba en false
		}
	}
}


/* Try to move the enemigo[lyd] tetrimino by dx, dy and return true if successful.
 */
bool move_bichito(s8 dx, s8 dy) // to move player
{
	if(!(aliado.existe)) return false; // if does not exist, no need to do anything
    if (collide(aliado.x + dx, aliado.y + dy))
        return false;
    aliado.x += dx;
    aliado.y += dy;
    return true;
}

bool move_bichito2(s8 dx, s8 dy) // to move player level 2
{
	if(!(aliado.existe)) return false; // if does not exist, no need to do anything
    if (collide2(aliado.x + dx, aliado.y + dy)){
        return false;
    }
    aliado.x += dx;
    aliado.y += dy;
    return true;
}

bool move_enemigo(s8 dx, s8 dy, s8 lol) // for enemies
{
    if(!(enemigo[lol].existe)) return false; // if does not exist, no need to do anything
    if (collide(enemigo[lol].x + dx, enemigo[lol].y + dy))
        return false;
    enemigo[lol].x += dx;
    enemigo[lol].y += dy;
    return true;
}

bool move_bala(s8 dx, s8 dy, s8 lol) // for bullets
{
    if((bala[lol].existe)== false) return false; // if does not exist, no need to do anything
    if (collide(bala[lol].x + dx, bala[lol].y + dy))
        return false;
    bala[lol].x += dx;
    bala[lol].y += dy;
    return true;
}

bool move_rocas(s8 dx, s8 dy, s8 lol) // for rocks
{
    if((rocas[lol].existe)== false) return false; // if does not exist, no need to do anything
    if (collide(rocas[lol].x + dx, rocas[lol].y + dy))
        return false;
    rocas[lol].x += dx;
    rocas[lol].y += dy;
    return true;
}

/* Update the game state. Called at an interval relative to the enemigo[lyd] level.
 */
void update(void)
{
	for (int lyd = 0; lyd < 4; lyd++){
	    if (!(move_enemigo(0, 1, lyd))) enemigo[lyd].existe = false;
	    if (!(move_bala(0, -1, lyd))) bala[lyd].existe = false; 
	}
	spawn();
}

void update2(void) // update for level 2
{
	for (int lyd = 0; lyd < 4; lyd++){
	    if (!(move_enemigo(0, 1, lyd))) enemigo[lyd].existe = false; 
	}
	for (int lyd = 0; lyd < 3; lyd++){
		if (!(move_rocas(0, 1, lyd))){
			score += 1;
			rocas[lyd].existe = false; 
		}
	}
	u32 temp_position[20];

	for (int lyd = 0; lyd < 20; lyd++){
		temp_position[lyd] = position[lyd];
	}

	for (int lyd = 0; lyd < 19; lyd++){
		position[lyd] = temp_position[lyd+1];
	}
	position[19] = temp_position[0];

	spawn2();
}

void disparar(void){ // to shoot the bullets
	for (int lyd = 0; lyd < 4; lyd++){
		if (bala[lyd].existe == false){
			bala[lyd].existe = true;
			bala[lyd].x = aliado.x + 1; // create it in front of the player position
			bala[lyd].y = aliado.y - 1;
			return;
		}
	}
}

#define TITLE_X (COLS / 2 - 9)
#define TITLE_Y (ROWS / 2 - 10)

/* Draw about information in the centre. Shown on boot and pause. */
void draw_about(void) {
    puts(TITLE_X,      TITLE_Y,     BLACK,            YELLOW, "   ");
    puts(TITLE_X + 3,  TITLE_Y,     BLACK,            YELLOW, "   ");
    puts(TITLE_X + 6,  TITLE_Y,     BLACK,            YELLOW, "   ");
    puts(TITLE_X + 9,  TITLE_Y,     BLACK,            YELLOW, "   ");
    puts(TITLE_X + 12, TITLE_Y,     BLACK,            YELLOW, "   ");
    puts(TITLE_X + 15, TITLE_Y,     BLACK,            YELLOW, "   ");
    puts(TITLE_X,      TITLE_Y + 1, BRIGHT | GRAY,    YELLOW, "   ");
    puts(TITLE_X + 3,  TITLE_Y + 1, BRIGHT | GRAY,    YELLOW, " L ");
    puts(TITLE_X + 6,  TITLE_Y + 1, BRIGHT | GRAY,    YELLOW, " E ");
    puts(TITLE_X + 9,  TITLE_Y + 1, BRIGHT | GRAY,    YELLOW, " A ");
    puts(TITLE_X + 12, TITLE_Y + 1, BRIGHT | GRAY,    YELLOW, " D ");
    puts(TITLE_X + 15, TITLE_Y + 1, BRIGHT | GRAY,    YELLOW, "   ");
    puts(TITLE_X,      TITLE_Y + 2, BLACK,            YELLOW, "   ");
    puts(TITLE_X + 3,  TITLE_Y + 2, BLACK,            YELLOW, "   ");
    puts(TITLE_X + 6,  TITLE_Y + 2, BLACK,            YELLOW, "   ");
    puts(TITLE_X + 9,  TITLE_Y + 2, BLACK,            YELLOW, "   ");
    puts(TITLE_X + 12, TITLE_Y + 2, BLACK,            YELLOW, "   ");
    puts(TITLE_X + 15, TITLE_Y + 2, BLACK,            YELLOW, "   ");

    puts(TITLE_X - 8, TITLE_Y + 6,  GRAY, BLACK, "Instituto Tecnologico de Costa Rica ");
    puts(TITLE_X - 8, TITLE_Y + 8,  GRAY, BLACK, "   Sistemas Operativos Empotrados   ");
    puts(TITLE_X - 8, TITLE_Y + 10, GRAY, BLACK, "      Limber Rodriguez Rojas        ");
    puts(TITLE_X - 8, TITLE_Y + 12, GRAY, BLACK, "      Daniela Viales Vasquez        ");
    puts(TITLE_X - 8, TITLE_Y + 15, GRAY, BLACK, " Profesor: Ernesto Rivera Alvarado  ");

    // aqui dibujo la figura al final de la portada
    puts(TITLE_X + 8,  TITLE_Y + 18,     BLACK,        YELLOW, "  ");
    puts(TITLE_X + 6,  TITLE_Y + 19,     BLACK,        YELLOW, "  ");
    puts(TITLE_X + 8,  TITLE_Y + 19,     BLACK,        YELLOW, "  ");
    puts(TITLE_X + 10,  TITLE_Y + 19,     BLACK,        YELLOW, "  ");

    puts(TITLE_X - 8, TITLE_Y + 21, GRAY, BLACK, "        Press P to continue       ");	
}

void draw_game_over(void){
	puts(TITLE_X,      TITLE_Y,     BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 3,  TITLE_Y,     BRIGHT | GRAY,            YELLOW, " G ");
    puts(TITLE_X + 6,  TITLE_Y,     BRIGHT | GRAY,            YELLOW, " A ");
    puts(TITLE_X + 9,  TITLE_Y,     BRIGHT | GRAY,            YELLOW, " M ");
    puts(TITLE_X + 12, TITLE_Y,     BRIGHT | GRAY,            YELLOW, " E ");
    puts(TITLE_X + 15, TITLE_Y,     BLACK,                    YELLOW, "   ");
    puts(TITLE_X,      TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 3,  TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 6,  TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 9,  TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 12, TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 15, TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X,      TITLE_Y + 2, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 3,  TITLE_Y + 2, BRIGHT | GRAY,            YELLOW, " O ");
    puts(TITLE_X + 6,  TITLE_Y + 2, BRIGHT | GRAY,            YELLOW, " V ");
    puts(TITLE_X + 9,  TITLE_Y + 2, BRIGHT | GRAY,            YELLOW, " E ");
    puts(TITLE_X + 12, TITLE_Y + 2, BRIGHT | GRAY,            YELLOW, " R ");
    puts(TITLE_X + 15, TITLE_Y + 2, BLACK,                    YELLOW, "   ");

    puts(TITLE_X - 10, TITLE_Y + 10, GRAY, BLACK, "          Press P to continue       ");	
}

void draw_level_2(void){
	puts(TITLE_X,      TITLE_Y,     BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 3,  TITLE_Y,     BRIGHT | GRAY,            YELLOW, " L ");
    puts(TITLE_X + 6,  TITLE_Y,     BRIGHT | GRAY,            YELLOW, " E ");
    puts(TITLE_X + 9,  TITLE_Y,     BRIGHT | GRAY,            YELLOW, " V ");
    puts(TITLE_X + 12, TITLE_Y,     BRIGHT | GRAY,            YELLOW, " E ");
    puts(TITLE_X + 15, TITLE_Y,     BRIGHT | GRAY,            YELLOW, " L ");
    puts(TITLE_X,      TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 3,  TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 6,  TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 9,  TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 12, TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 15, TITLE_Y + 1, BLACK,                    YELLOW, "   ");
    puts(TITLE_X,      TITLE_Y + 2, BLACK,                    YELLOW, "   ");
    puts(TITLE_X + 3,  TITLE_Y + 2, BRIGHT | GRAY,            YELLOW, "   ");
    puts(TITLE_X + 6,  TITLE_Y + 2, BRIGHT | GRAY,            YELLOW, "   ");
    puts(TITLE_X + 9,  TITLE_Y + 2, BRIGHT | GRAY,            YELLOW, " 2 ");
    puts(TITLE_X + 12, TITLE_Y + 2, BRIGHT | GRAY,            YELLOW, "   ");
    puts(TITLE_X + 15, TITLE_Y + 2, BLACK,                    YELLOW, "   ");

    puts(TITLE_X - 10, TITLE_Y + 10, GRAY, BLACK, "          Press P to continue       ");	
}

#define WELL_X (COLS / 2 - WELL_WIDTH)

#define PREVIEW_X (COLS * 3/4 + 1)
#define PREVIEW_Y (2)

#define STATUS_X (COLS * 3/4)
#define STATUS_Y (ROWS / 2 - 4)

#define SCORE_X STATUS_X
#define SCORE_Y (ROWS / 2 - 1)

#define LEVEL_X SCORE_X
#define LEVEL_Y (SCORE_Y + 4)

#define VIDAS_X SCORE_X
#define VIDAS_Y (SCORE_Y + 8)

void draw(int posicion) // position goes for 0 to 3, 
{
    u8 x, y;

    if (paused) {
        draw_about();
        goto status;
    }

    /* Border */ // para borrar las paredes que se mueven
    for (y = 2; y <= WELL_HEIGHT; y++) {
        putc(WELL_X - 1,            y, GRAY, BLACK, ' ');
        putc(COLS / 2 + WELL_WIDTH, y, GRAY, BLACK, ' ');
    }

    /* Border */ // para pintar las paredes
    for (y = 2 + posicion; y <= WELL_HEIGHT; y+=4) {
        putc(WELL_X - 1,            y, BLACK, GRAY, ' ');
        putc(COLS / 2 + WELL_WIDTH, y, BLACK, GRAY, ' ');
    }
    for (x = 1; x < WELL_WIDTH * 2 + 1; x++)
        putc(WELL_X + x - 1, WELL_HEIGHT, BRIGHT, BLACK, ':');

    /* Well */
    for (y = 0; y < 2; y++)
        for (x = 0; x < WELL_WIDTH; x++)
            puts(WELL_X + x * 2, y, BLACK, BLACK, "  ");
    for (y = 2; y < WELL_HEIGHT; y++)
        for (x = 0; x < WELL_WIDTH; x++)
            puts(WELL_X + x * 2, y, BRIGHT, BLACK, "::");

    /* enemigo[lyd] */
    for (int lyd = 0; lyd < 4; lyd++){
    	if (enemigo[lyd].existe == true)
		    for (y = 0; y < 2; y++)
		        for (x = 0; x < 3; x++)
		            if (TETRIS[enemigo[lyd].i][y][x])
		                puts(WELL_X + enemigo[lyd].x * 2 + x * 2, enemigo[lyd].y + y, BLACK,
		                     TETRIS[enemigo[lyd].i][y][x], "  ");
    }

    /* bala[lyd] */
    for (int lyd = 0; lyd < 4; lyd++){
    	if (bala[lyd].existe == true)
            puts(WELL_X + bala[lyd].x * 2, bala[lyd].y, 4, BLACK, "ll");
    }

    // aliado
    if (aliado.existe == true)
	    for (y = 0; y < 2; y++)
		        for (x = 0; x < 3; x++)
		            if (TETRIS[aliado.i][y][x])
		                puts(WELL_X + aliado.x * 2 + x * 2, aliado.y + y, BLACK,
		                     TETRIS[aliado.i][y][x], "  ");

status:
    if (paused)
        puts(STATUS_X + 2, STATUS_Y, BRIGHT | 1, BLACK, "PAUSED");
    if (game_over)
        puts(STATUS_X, STATUS_Y, BRIGHT | RED, BLACK, "GAME OVER");

    /* Score */
    puts(SCORE_X + 6, SCORE_Y, GRAY, BLACK, "SCORE");
    puts(SCORE_X  + 4, SCORE_Y + 2, BRIGHT | GRAY, BLACK, itoa(score, 10, 10));

    /* Level */
    puts(LEVEL_X + 6, LEVEL_Y, GRAY, BLACK, "LEVEL");
    puts(LEVEL_X + 4, LEVEL_Y + 2, BRIGHT | GRAY, BLACK, itoa(level, 10, 10));

    /* VIDAS */
    puts(VIDAS_X + 6, VIDAS_Y, GRAY, BLACK, "VIDAS");
    puts(VIDAS_X  + 4, VIDAS_Y + 2, BRIGHT | GRAY, BLACK, itoa(vidas, 10, 10));
}

void draw2(int posicion) // position is a number from 0 to 3, for level 2
{
    u8 x, y;

    if (paused) {
        draw_about();
        goto status;
    }

    /* Border */ // para borrar las paredes que se mueven
    for (y = 2; y <= WELL_HEIGHT; y++) {
        putc(WELL_X - 1,            y, GRAY, BLACK, ' ');
        putc(COLS / 2 + WELL_WIDTH, y, GRAY, BLACK, ' ');
    }

    /* Border */ // para pintar las paredes
    for (y = 2 + posicion; y <= WELL_HEIGHT; y+=4) {
        putc(WELL_X - 1,            y, BLACK, GRAY, ' ');
        putc(COLS / 2 + WELL_WIDTH, y, BLACK, GRAY, ' ');
    }


    for (x = 1; x < WELL_WIDTH * 2 + 1; x++)
        putc(WELL_X + x - 1, WELL_HEIGHT, BRIGHT, BLACK, ':');// para pintar la fila de abajo

    /* Well */
    for (y = 0; y < 2; y++)
        for (x = 0; x < WELL_WIDTH; x++)
            puts(WELL_X + x * 2, y, BLACK, BLACK, "  ");
    for (y = 2; y < WELL_HEIGHT; y++)
        for (x = 0; x < WELL_WIDTH; x++)
			puts(WELL_X + x * 2, y, BRIGHT, BLACK, "::");

    // aliado
    if (aliado.existe == true)
	    for (y = 0; y < 2; y++)
		        for (x = 0; x < 3; x++)
		            if (TETRIS[aliado.i][y][x])
		                puts(WELL_X + aliado.x * 2 + x * 2, aliado.y + y, BLACK,
		                     TETRIS[aliado.i][y][x], "  ");

	/* Rocas */
    for (int lyd = 0; lyd < 4; lyd++){
    	if (rocas[lyd].existe == true)
            puts(WELL_X + rocas[lyd].x * 2, rocas[lyd].y, 4, 4, "  ");
    }

	u32 temp = WELL_HEIGHT;

	for (x = 0; x < 19; x++){
		puts(WELL_X + position[x] * 2, temp, BLACK, GRAY, "  ");
		puts(WELL_X + (position[x] * 2) + 11*2, temp, BLACK, GRAY, "  ");
		temp -= 1;
	} 

status:
    if (paused)
        puts(STATUS_X + 2, STATUS_Y, BRIGHT | 1, BLACK, "PAUSED");
    if (game_over)
        puts(STATUS_X, STATUS_Y, BRIGHT | RED, BLACK, "GAME OVER");

    /* Score */
    puts(SCORE_X + 6, SCORE_Y, GRAY, BLACK, "SCORE");
    puts(SCORE_X  + 4, SCORE_Y + 2, BRIGHT | GRAY, BLACK, itoa(score, 10, 10));

    /* Level */
    puts(LEVEL_X + 6, LEVEL_Y, GRAY, BLACK, "LEVEL");
    puts(LEVEL_X + 4, LEVEL_Y + 2, BRIGHT | GRAY, BLACK, itoa(level, 10, 10));

    /* VIDAS */
    puts(VIDAS_X + 6, VIDAS_Y, GRAY, BLACK, "VIDAS");
    puts(VIDAS_X  + 4, VIDAS_Y + 2, BRIGHT | GRAY, BLACK, itoa(vidas, 10, 10));
}

int pos = 0;

noreturn kernel_main()
{

loop0:

    clear(BLACK);
    draw_about();
    inicializar();

    u8 key;
    u8 last_key;

    // wait for enter to start the game
    while (1){
    	if ((key=scan())){
    		if (key == KEY_P) break;
    	}
    }

    /* Wait a full second to calibrate timing. */
    u32 itpms;
    tps();
    itpms = tpms; while (tpms == itpms) tps();
    itpms = tpms; while (tpms == itpms) tps();

    /* Initialize game state. Shuffle bag of tetriminos until first tetrimino
     * is not S or Z. */
    //do { shuffle(bag, BAG_SIZE); } while (bag[0] == 4 || bag[0] == 6);
    spawn();
    clear(BLACK);
    draw(pos);

loop:	

    tps();

    puts(1, 16, BRIGHT | GRAY, BLACK, "SPACE");
    puts(7, 16, GRAY,          BLACK, "- Shoot");
    puts(1, 17, BRIGHT | GRAY, BLACK, "P");
    puts(7, 17, GRAY,          BLACK, "- Pause");
    puts(1, 18, BRIGHT | GRAY, BLACK, "R");
    puts(7, 18, GRAY,          BLACK, "- Reset");

    bool updated = false;

    if ((key = scan())) {
        last_key = key;
        switch(key) {
        case KEY_R:
            reset();
        case KEY_LEFT:
            move_bichito(-1, 0);
            break;
        case KEY_RIGHT:
            move_bichito(1, 0);
            break;
        case KEY_SPACE:
            disparar();
            break;
        case KEY_P:
            if (game_over)
                break;
            clear(BLACK);
            paused = !paused;
            break;
        }
        updated = true;
    }

    if (!paused && !game_over && interval(TIMER_UPDATE, speed)) {
        update();
        updated = true;
        if (pos < 4) {
    		pos++;
	    }
	    if (pos == 4) {
	    	pos = 0;
	    }
    }

    if (updated) {
        draw(pos);
        check_collisions();
        check_level_change();
        check_game_over();
        if (game_over){
			clear(BLACK);
			goto loop2;
		}
		if (level2 == true){
			clear(BLACK);
			goto loop4;
		}
    }

    goto loop;

loop2:
	draw_game_over();

	if ((key=scan())){
		if (key == KEY_P){ 
			vidas = 3;
			score = 0;
			level = 1;
			game_over = false;
			level2 = false;
			goto loop0;
		}
	}
	

	goto loop2;

loop3:


    puts(1, 17, BRIGHT | GRAY, BLACK, "P");
    puts(7, 17, GRAY,          BLACK, "- Pause");
    puts(1, 18, BRIGHT | GRAY, BLACK, "R");
    puts(7, 18, GRAY,          BLACK, "- Reset");

    updated = false;

    if ((key = scan())) {
        last_key = key;
        switch(key) {
        case KEY_R:
            reset();
        case KEY_LEFT:
            move_bichito2(-1, 0);
            break;
        case KEY_RIGHT:
            move_bichito2(1, 0);
            break;
        case KEY_P:
            if (game_over)
                break;
            clear(BLACK);
            paused = !paused;
            break;
        }
        updated = true;
    }

    if (!paused && !game_over && interval(TIMER_UPDATE, speed)) {
        update2();
        updated = true;
        if (pos < 4) {
    		pos++;
	    }
	    if (pos == 4) {
	    	pos = 0;
	    }
    }

    if (updated) {
    	collide2(aliado.x, aliado.y);
        draw2(pos);
        check_collisions_rocas();
        check_game_over();
        if (game_over){
			clear(BLACK);
			goto loop2;
		}
		if (score >= 35){
			clear(BLACK);
			score = 0;
			level2 = false;
			level = 1;
			vidas = 3;
			goto loop0;
		}
    }

    goto loop3;

loop4:

	draw_level_2();

	if ((key=scan())){
		if (key == KEY_P){ 
			inicializar2();
			clear(BLACK);
			goto loop3;
		}
	}

	goto loop4;



}
