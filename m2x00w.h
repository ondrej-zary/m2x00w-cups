/* CUPS driver for Minolta magicolor 2300W/2400W/2500W printers */
/* Copyright (c) 2014 Ondrej Zary */
#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t

#define POINTS_PER_INCH 72

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define ROUND_UP_MULTIPLE(n, m) (((n) + (m) - 1) & ~((m) - 1))

#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN
#else
#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#endif

#define BLOCKS_PER_PAGE		8

#define M2X00W_MAGIC		0x1B
struct header {
	unsigned char magic;	/* 0x1B */
	unsigned char type;		/* block type */
	unsigned char seq;		/* block sequence number */
	unsigned short len;		/* data length (little endian) */
	unsigned char type_inv;	/* type ^ 0xff */
	unsigned char data[0];
} __attribute__((packed));

#define M2X00W_BLOCK_BEGIN	0x40
#define M2X00W_BLOCK_PARAMS	0x50
#define M2X00W_BLOCK_PAGE	0x51
#define M2X00W_BLOCK_DATA	0x52
#define M2X00W_BLOCK_ENDPART	0x55
#define M2X00W_BLOCK_END	0x41

enum m2x00w_model { M2300W = 0x82, M2400W = 0x85, M2500W = 0x87 };

struct block_begin {
	unsigned char model;	/* 0x82 = 2300W, 0x85 = 2400W, 0x87 = 2500W */
	unsigned char color;	/* 0x10 */
} __attribute__((packed));

enum m2x00w_res_y { RES_300DPI = 0x00, RES_600DPI, RES_1200DPI };
enum mx200w_res_x { RES_MULT1 = 0x00, RES_MULT2, RES_MULT4 };

struct block_params {
	unsigned char res_y;	/* 00=300dpi, 01=600dpi, 02=1200dpi */
	unsigned char res_x;	/* 00=res_y, 01=2*res_y, 02=4*res_y */
	unsigned char zeros[6];
} __attribute__((packed));

enum m2x00w_color_mode { MODE_BW_2300 = 0x00, MODE_BW = 0x80, MODE_COLOR = 0xf0 };
enum m2x00w_paper_size {
	PAPER_A4		= 0x04,
	PAPER_B5_JIS		= 0x06,
	PAPER_A5		= 0x08,
	PAPER_J_POSTCARD	= 0x0C,
	PAPER_D_POSTCARD	= 0x0D,
	PAPER_ENV_YOU4		= 0x0F,
	PAPER_FOLIO		= 0x12,
	PAPER_KAI32		= 0x15,
	PAPER_LEGAL		= 0x19,
	PAPER_G_LEGAL		= 0x1A,
	PAPER_LETTER		= 0x1B,
	PAPER_G_LETTER		= 0x1D,
	PAPER_EXECUTIVE		= 0x1F,
	PAPER_STATEMENT		= 0x21,
	PAPER_ENV_MONARCH	= 0x24,
	PAPER_ENV_COM10		= 0x25,
	PAPER_ENV_DL		= 0x26,
	PAPER_ENV_C5		= 0x27,
	PAPER_ENV_C6		= 0x28,
	PAPER_B5_ISO		= 0x29,
	PAPER_ENV_CHOU3		= 0x2D,
	PAPER_ENV_CHOU4		= 0x2E,
	PAPER_CUSTOM		= 0x31,
	PAPER_FOOLSCAP		= 0x46,
	PAPER_16K		= 0x51,
	PAPER_KAI16		= 0x52,
	PAPER_LETTER_PLUS	= 0x53,
	PAPER_UK_QUARTO		= 0x54,
	PAPER_PHOTO_10X15	= 0x65,
};

struct block_page {
	unsigned char color_mode;	/* 0xF0 = color, 0x80 = BW (2400W/2500W), 0x00 = BW (2300W) */
	unsigned char copies;	/* number of copies (only 2500W, always 01 for 2300W/2400W) */
	unsigned short x_start;
	unsigned short x_end;
	unsigned short y_start;
	unsigned short y_end;
	unsigned short blocks1;	/* blocks per page */
	unsigned short blocks2;	/* blocks per page (again?) */
	unsigned char tray;
	unsigned char paper_size;
	unsigned short custom_width;/* custom size - width in mm */
	unsigned short custom_height;/* custom size - height in mm */
	unsigned char zero1;
	unsigned char duplex;	/* 0x80 = duplex on (2300W only)*/
	unsigned char paper_weight;
	unsigned char zero2;
	unsigned char unknown;	/* 01 for 2300W or ???, else 00 */
	unsigned char zeros[3];
} __attribute__((packed));

enum m2x00w_color { COLOR_K = 0, COLOR_C, COLOR_M, COLOR_Y };

struct block_data {
	unsigned int data_len;
	unsigned char color;	/* 00=K, 01=C, 02=M, 03=Y */
	unsigned char block_num;
	unsigned short lines;	/* lines per block */
} __attribute__((packed));

struct block_endpart {
	unsigned char type;	/* 00=end job 10=wait for button (manual duplex) */
} __attribute__((packed));
