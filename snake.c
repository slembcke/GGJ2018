#include <stdlib.h>

#include "snake.h"
#include "levels.h"

#define THROTTLE (4)

struct {
    u8 head_x, head_y;
    u8 sig_str;
    u8 throttle_ctr;
    u8 dirs_available;
    enum {UP=EVENT_UP, DOWN=EVENT_DW, LEFT=EVENT_LF, RIGHT=EVENT_RT, STILL} state;
    u8 dir;
    u16 sig_dir;
    u8 tiles_covered;
    snake_status_t victory_condition;
    
    Level level;
    u8 message_length;
} state;


#define SNAKE_HEAD 0x14
#define SNAKE_BODY 0x15
#define MIN_COORD 1
#define MAX_COORD 14

#define BIG_TILE_UPDATE_SIZE 10
#define BIG_TILE_MAX_COUNT 4
#define POW(x) (0x0001 << (x))

#define OFFX 2
#define OFFY 2

static char *LEVELTEXT[] = {
    "Press B: boost fading signal",
    "This message will fade",
    "It was fun wasn't it",
    "I think so anyway",
    "Is your sister still working?",
    "Interesting life choice",
    "I think so anyway",
    "Thank you for transmissing",
    NULL,
};


static const u8 SIGNAL_UP_MSPRITE[] = {
     0,  0, 0x8C, 0,
     8,  0, 0x8D, 0,
     0,  8, 0x9C, 0,
     8,  8, 0x9D, 0,
    MSPRITE_END
};

static const u8 SIGNAL_DOWN_MSPRITE[] = {
     0,  0, 0x9C, SPR_FLIP_Y,
     8,  0, 0x9D, SPR_FLIP_Y,
     0,  8, 0x8C, SPR_FLIP_Y,
     8,  8, 0x8D, SPR_FLIP_Y,
    MSPRITE_END
};

static const u8 SIGNAL_RIGHT_MSPRITE[] = {
     0,  0, 0xAC, 0,
     8,  0, 0xAD, 0,
     0,  8, 0xBC, 0,
     8,  8, 0xBD, 0,
    MSPRITE_END
};

static const u8 SIGNAL_LEFT_MSPRITE[] = {
     0,  0, 0xAC, SPR_FLIP_X,
     8,  0, 0xAD, SPR_FLIP_X,
     0,  8, 0xBC, SPR_FLIP_X,
     8,  8, 0xBD, SPR_FLIP_X,
    MSPRITE_END
};

static const u8 * const SIGNAL_DIRECTIONS[] = {
    SIGNAL_UP_MSPRITE,
    SIGNAL_DOWN_MSPRITE,
    SIGNAL_LEFT_MSPRITE,
    SIGNAL_RIGHT_MSPRITE,
};


u8 up_buff[BIG_TILE_UPDATE_SIZE*BIG_TILE_MAX_COUNT];
u8 up_i = 0;
u8 block_lu[2][2];
u16 collision_map[16];
const u16 pow2[16] = {
    POW(0), POW(1), POW(2), POW(3), POW(4), POW(5), POW(6), POW(7),
    POW(8), POW(9), POW(10), POW(11), POW(12), POW(13), POW(14), POW(15)
};

void set_tile(u8 x, u8 y, u8 c) {
    u16 tile_hi;
    u16 tile_lo;

    if (up_i > (BIG_TILE_UPDATE_SIZE-1)*BIG_TILE_MAX_COUNT) {
        up_i=0;
        set_tile(0,0,'!');
        return;
    }
    tile_hi = NTADR_A(x<<1, y<<1);
    tile_lo = NTADR_A(x<<1, (y<<1)+1);
    
    (up_buff + 0)[up_i] = NT_UPD_HORZ | (tile_hi>>8)&0xFF;
    (up_buff + 1)[up_i] = (tile_hi>>0)&0xFF;
    (up_buff + 2)[up_i] = 2;
    (up_buff + 3)[up_i] = TILECOORDS(c,0,0);
    (up_buff + 4)[up_i] = TILECOORDS(c,0,1);

    (up_buff + 5)[up_i] = NT_UPD_HORZ | (tile_lo>>8)&0xFF;
    (up_buff + 6)[up_i] = (tile_lo>>0)&0xFF;
    (up_buff + 7)[up_i] = 2;
    (up_buff + 8)[up_i] = TILECOORDS(c,1,0);
    (up_buff + 9)[up_i] = TILECOORDS(c,1,1);

    up_i+= BIG_TILE_UPDATE_SIZE;

    up_buff[up_i] = NT_UPD_EOF;
}

void set_tile_quarter_asteroid(u8 x, u8 y) {
    u16 tile_hi;
    tile_hi = NTADR_A(x, y);
    
    (up_buff + 0)[up_i] = NT_UPD_HORZ | (tile_hi>>8)&0xFF;
    (up_buff + 1)[up_i] = (tile_hi>>0)&0xFF;
    (up_buff + 2)[up_i] = 1;
    (up_buff + 3)[up_i] = asteroidCornerIndex[(x + y<<1 ) % 8];

    up_i+= 4;

    up_buff[up_i] = NT_UPD_EOF;
}

void clear_up(void) {
    up_buff[0] = NT_UPD_EOF;
    up_i=0;
}

void snake_draw_task(void) {
    // TODO actually draw snake
    // May need to rearrange this
    // ppu_off(); {
    //     vram_adr(NTADR_A(state.head_x, state.head_y));
    //     // vram_adr(NTADR_A(7, 7));
    //     vram_put(0x14);
        
    // } ppu_on_all();
}

void snake_draw_post(void) {
    clear_up();
}

void set_state(u32 new_state)
{
    state.state = new_state;
    if(new_state < STILL) {
        sfx_play(SFX_MORSE, 1);
        state.dir = new_state;
    }
}

void find_dirs_avail(void) {
    u16 row=0;
    u16 mask, left, right, above, below;

    row = collision_map[state.head_y];
    mask = pow2[state.head_x];
    left = mask >> 1;
    left &= row;
    right = mask << 1;
    right &= row;
    above = (collision_map-1)[state.head_y];
    below = (collision_map+1)[state.head_y];
    
    state.dirs_available = 0;
    
    if((above&mask)==0){
        state.dirs_available |= (pow2[EVENT_UP]);
    }
    if((below&mask)==0){
        state.dirs_available |= (pow2[EVENT_DW]);
    }
    if(left==0){
        state.dirs_available |= (pow2[EVENT_LF]);
    }
    if(right==0){
        state.dirs_available |= (pow2[EVENT_RT]);
    }
}

snake_status_t snake_success(){
    if (state.tiles_covered == (LEVEL_SIZE*LEVEL_SIZE - LEVEL_BLOCKERS - 1))
        return SNAKE_WIN;
    else if(state.victory_condition == SNAKE_LOSS)
        return SNAKE_LOSS;
    else
        return IN_PROGRESS;
}

const char HEX[] = "0123456789ABCDEF";

u8 is_start(void) {
    return (state.head_x == state.level.start_x &&
        state.head_y == state.level.start_y);
}

u8 is_end(void) {
    return (state.head_x == state.level.end_x &&
        state.head_y == state.level.end_y);
}

void signal_died(void) {
    state.victory_condition = SNAKE_LOSS;
}

void snake_task(void) {
    {
        register u8 x = state.head_x*16;
        register u8 y = state.head_y*16;
		register u8 clock = (nesclock() >> 1) & 15;
		if(clock > 8) clock = 16 - clock;
        if(state.dir == LEFT || state.dir == RIGHT){
            x += clock - 4;
        }
        if(state.dir == UP || state.dir == DOWN){
            y += clock - 4;
        }
        
        oam_meta_spr_pal(x, y, 0, state.sig_dir);
    }
    
    if(!is_start())
    {
        if((state.sig_str >= 0) && 0==(state.throttle_ctr%16)) {
            if(state.sig_str == 0) {
                signal_died();
            }
            else {
                u16 tile_hi = NTADR_A(3 + state.sig_str, 3);
                (up_buff + 0)[up_i] = NT_UPD_HORZ | (tile_hi>>8)&0xFF;
                (up_buff + 1)[up_i] = (tile_hi>>0)&0xFF;
                (up_buff + 2)[up_i] = 1;
                (up_buff + 3)[up_i] = 0x00;
                up_i+= 4;
                up_buff[up_i] = NT_UPD_EOF;
                
                state.sig_str -= 1;
            }
        }
    }
    state.throttle_ctr += 1;
    if(!(state.dirs_available & pow2[state.state]))
    {
        set_state(STILL);
    }
    else {
        if(0==(state.throttle_ctr%THROTTLE)) {
            if(!is_start() && !is_end())
            {
                u8 c = (((state.head_y << 1) + state.head_x) & 7) << 1;
                set_tile(state.head_x, state.head_y, 0xE0 + c); //0xA6=spacedust
            }
            collision_map[state.head_y] |= pow2[state.head_x];
            if(state.state==DOWN) {
                state.head_y += 1;
                state.tiles_covered++;
            }
            if(state.state==UP) {
                state.head_y -= 1;
                state.tiles_covered++;
            }
            if(state.state==RIGHT) {
                state.head_x += 1;
                state.tiles_covered++;
            }
            if(state.state==LEFT) {
                state.head_x -= 1;
                state.tiles_covered++;
            }
            state.sig_dir = SIGNAL_DIRECTIONS[state.state];
            find_dirs_avail();
            state.sig_str = 24;
            
            {
                u16 tile_hi = NTADR_A(4, 3);
                (up_buff + 0)[up_i] = NT_UPD_HORZ | (tile_hi>>8)&0xFF;
                (up_buff + 1)[up_i] = (tile_hi>>0)&0xFF;
                (up_buff + 2)[up_i] = 24;
                memfill(up_buff + 3 + up_i, 0x19, 24);
                up_i += 3 + 24;
                up_buff[up_i] = NT_UPD_EOF;
            }
        }
    }
}


void snake_init(void) {
    register u8 i=0;
    u16 row=0x01, mask=0;
    register u8 x=0,y=0;
    
    memcpy(&state.level, LEVELS + CURRENT_LEVEL, sizeof(state.level));
    state.level.start_x += OFFX;
    state.level.start_y += OFFY;
    state.level.end_x += OFFX;
    state.level.end_y += OFFY;
    
    // Clear background with random-ish sparse stars.
    {
        vram_adr(NTADR_A(0, 0));
        
        for(iy = 0; iy < 15; ++iy){
            for(ix = 0; ix < 16; ++ix){
                u8 c = (((iy << 1) + ix) & 7) << 1;
                vram_put(0xC0 + c);
                vram_put(0xC1 + c);
            }
            
            for(ix = 0; ix < 16; ++ix){
                u8 c = (((iy << 1) + ix) & 7) << 1;
                vram_put(0xD0 + c);
                vram_put(0xD1 + c);
            }
        }

    }
    
    memfill(collision_map, 0xFF, sizeof(collision_map));
    memcpy(collision_map + 2, state.level.map, sizeof(state.level.map));

    for(x=0;x<16;x++) {
        mask = pow2[x];
        for(y=0;y<15;y++) {
            if( (collision_map[y]&mask)==mask) {
                DRAWTILE(x*2,y*2, asteroidCornerIndex[(x + (y << 1) ) % 8] );
            }
        }
    }
    vram_adr(NTADR_A(0, 30) );
        
    for(y=0;y<8;y++) {
        u8 value;
        for(x=0;x<8;x++) {
            u16 maskR = pow2[x * 2 + 1]; // TODO: should shift by this amount instead of masking with a 16 bit number.
            u16 maskL = pow2[x * 2];
            // collect all 4 tiles:
            // palette 0x01 is the asteroid palette 
            
            value = ((collision_map[y * 2]&maskL ) == maskL)  
                  | ((collision_map[y * 2]&maskR) == maskR) << 2 
                  | ((collision_map[y * 2 + 1]&maskL) == maskL) << 4 
                  | ((collision_map[y * 2 + 1]&maskR) == maskR) << 6;
            vram_put(value << 1);  // 10 10 10 10
        }
    }

    vram_adr(NTADR_A(2, 2));
    state.message_length = strlen(LEVELTEXT[0]);
    for(ix = 0; ix<state.message_length; ++ix)  {
        vram_put(LEVELTEXT[0][ix]);
    }
    vram_adr(NTADR_A(4, 3));
    for(ix = 0; ix<2*LEVEL_SIZE; ++ix)  {
        vram_put(0x19);
    }

 
    state.sig_dir = SIGNAL_UP_MSPRITE;
    state.tiles_covered = 0;
    x=state.level.start_x;
    y=state.level.start_y;
    state.victory_condition = IN_PROGRESS;
    state.head_x = x;
    state.head_y = y;
    DRAWTILE_GRID(x,y, 0xAA); //0xAA satelite
    x=state.level.end_x;
    y=state.level.end_y;
    DRAWTILE_GRID(x,y, 0xAA); //0xAA satelite
    state.throttle_ctr = 0;
    state.sig_str = 24;
    state.state = STILL;

    find_dirs_avail();
    clear_up();
    // set_tile(state.head_x, state.head_y, '~'); //~ radiowave
    set_vram_update(up_buff);
}

u8 get_dir(s16 ship_vx, s16 ship_vy) {
    u16 mag_x, mag_y;
    mag_x = abs(ship_vx);
    mag_y = abs(ship_vy);
    if(ship_vx >0 && ship_vy==0) {
        return RIGHT;
    }
    else if(ship_vx <0 && ship_vy==0) {
        return LEFT;
    }
    else if(ship_vy >0 && ship_vx==0) {
        return DOWN;
    }
    else if(ship_vy <0 && ship_vx==0) {
        return UP;
    }
    return STILL;
}

void snake_event(u8 ship_x, u8 ship_y, s16 ship_vx, s16 ship_vy)
{
    u8 dir = get_dir(ship_vx, ship_vy);

    if(
        16*state.head_x - 8 <= ship_x && ship_x <= 16*state.head_x + 24 &&
        16*state.head_y - 8 <= ship_y && ship_y <= 16*state.head_y + 24
    ){
        if((pow2[dir])&state.dirs_available) {
            if(dir==STILL) {
                // this is probably a diagonal movement
                // but we probably don'w want to do anything in that case
            }
            else if(dir==EVENT_DW){
                set_state(DOWN);
            }
            else if(dir==EVENT_UP){
                set_state(UP);
            }
            else if(dir==EVENT_LF){
                set_state(LEFT);
            }
            else if(dir==EVENT_RT){
                set_state(RIGHT);
            }
        }
    }
}
