/* CUPS driver for Minolta magicolor 2300W/2400W/2500W printers */
/* Copyright (c) 2014 Ondrej Zary */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include "m2x00w.h"

#define DEBUG

#define ERR(fmt, args ...)	fprintf(stderr, "ERROR: M2X00W " fmt "\n", ##args);
#define WARN(fmt, args ...)	fprintf(stderr, "WARNING: M2X00W " fmt "\n", ##args);

#ifdef DEBUG
#define DBG(fmt, args ...)	fprintf(stderr, "DEBUG: M2X00W " fmt "\n", ##args);
#else
#define DBG(fmt, args ...)	do {} while (0)
#endif

enum m2x00w_model model;
u8 block_seq;
u8 *buf;
int buf_size, buf_pos;
//u16 line_len;
u16 line_len_file;
int width, height, dpi;

u8 checksum(void *p, int length)
{
	u8 sum = 0;
	u8 *data = p;

	for (int i = 0; i < length; i++) {
		sum += *data;
		data++;
	}

	return sum;
}

void write_block(u8 block_type, void *data, u8 data_len, FILE *stream)
{
	struct header header;
	u8 sum;

	header.magic = M2X00W_MAGIC;
	header.type = block_type;
	header.seq = block_seq++;
	header.len = cpu_to_le16(data_len);
	header.type_inv = block_type ^ 0xff;
	sum = checksum(&header, sizeof(header)) + checksum(data, data_len);

	fwrite(&header, 1, sizeof(header), stream);
	fwrite(data, 1, data_len, stream);
	fwrite(&sum, 1, sizeof(sum), stream);
}

int fls(unsigned int n) {
	int i = 0;

	while (n >>= 1)
		i++;

	return i;
}

enum m2x00w_paper_size encode_paper_size(const char *paper_size_name) {
	if (!strcmp(paper_size_name, "A4"))
		return PAPER_A4;
	else if (!strcmp(paper_size_name, "A5"))
		return PAPER_A5;
//	else if (!strcmp(paper_size_name, "B5"))
//		return PAPER_B5;
	else if (!strcmp(paper_size_name, "Letter"))
		return PAPER_LETTER;
	else if (!strcmp(paper_size_name, "Legal"))
		return PAPER_LEGAL;
	else if (!strcmp(paper_size_name, "Executive"))
		return PAPER_EXECUTIVE;
	else if (!strcmp(paper_size_name, "Monarch"))
		return PAPER_ENV_MONARCH;
	else if (!strcmp(paper_size_name, "Env10"))
		return PAPER_ENV_COM10;
	else if (!strcmp(paper_size_name, "DL"))
		return PAPER_ENV_DL;
	else if (!strcmp(paper_size_name, "C5"))
		return PAPER_ENV_C5;
	else
		return PAPER_CUSTOM;
}

void buf_add(void *data, int len) {
	if (buf_pos + len > buf_size) {
		ERR("buffer overflow");
		exit(1);
	}
	memcpy(buf + buf_pos, data, len);
	buf_pos += len;
}

u32 encode_raw(u8 *data, int len) {
	u32 out_len = 0;

//	DBG("%d raw bytes\n", len);
	while (len > 0) {
		u8 chunk = (len > 64) ? 64 : len;
		u8 count = chunk - 1;

		buf_add(&count, 1);
		buf_add(data, chunk);
		out_len += chunk + 1;
		data += chunk;
		len -= chunk;
	}

	return out_len;
}

u32 encode_rle(u8 byte, int count) {
	u8 repeat;
	u32 out_len = 0;

//	DBG("%d times 0x%02x\n", count, byte);
	if (count >= 4096) {
		/* encode 4096B run as two 2048B runs (happens only on 2400W at 2400dpi) */
		repeat = 0xe0;
		buf_add(&repeat, 1);
		buf_add(&byte, 1);
		buf_add(&repeat, 1);
		buf_add(&byte, 1);
		out_len += 4;
		count -= 4096;
	}
	if (count / 64 > 0) {
		repeat = 0xc0 + count / 64;
		buf_add(&repeat, 1);
		buf_add(&byte, 1);
		out_len += 2;
		count -= count / 64 * 64;
	}
	if (count > 0) {
		repeat = 0x80 + count;
		buf_add(&repeat, 1);
		buf_add(&byte, 1);
		out_len += 2;
	}

	return out_len;
}

u32 encode_line(u8 *data, int len) {
	u8 last = data[0];
	int raw_pos = 0, run_len = 0;
	u8 empty_table = 0x80;
	u32 out_len = 0;

	buf_add(&empty_table, 1);
	out_len += 1;

	for (int i = 0; i < len; i++) {
//		DBG("i=%d ", i);
		if (data[i] == last) {
//			DBG("run_len++");
			run_len++;
		} else {
			if (run_len > 2) {
				out_len += encode_raw(data + raw_pos, i - raw_pos - run_len);
				out_len += encode_rle(last, run_len);
				raw_pos = i;
			}
//			DBG("run_len = 0");
			run_len = 1;
		}
		last = data[i];
//		DBG("\n");
	}
//	DBG("flush\n");
	if (run_len < 3)
		run_len = 0;
	out_len += encode_raw(data + raw_pos, len - raw_pos - run_len);
	out_len += encode_rle(last, run_len);

	/* padding for 2500W */
	if (model == M2500W) {
		char pad_header[2];
		/* compute padding length for the row length to be multiple of 4 */
		char padlen = (4 - ((out_len + sizeof(pad_header)) % 4)) % 4;
		char padding[] = { 0xff, 0xff, 0xff };
		int rowlen = out_len + sizeof(pad_header) + padlen;

		pad_header[0] = (padlen << 6) | ((rowlen >> 8) & 0x3f);
		pad_header[1] = rowlen & 0xff;
		/* make space for pad_header and padding */
		memmove(buf + buf_pos - out_len + 1 + sizeof(pad_header) + padlen, buf + buf_pos - out_len + 1, out_len);
		/* insert pad_header and padding */
		memcpy(buf + buf_pos - out_len + 1, pad_header, sizeof(pad_header));
		memcpy(buf + buf_pos - out_len + 1 + sizeof(pad_header), padding, padlen);
		buf[buf_pos - out_len] |= 0x40;
		buf_pos += sizeof(pad_header) + padlen;
		out_len += sizeof(pad_header) + padlen;
	}

	return out_len;
}

void write_data_block(FILE *stream, enum m2x00w_color color, u8 *buf, u32 len, u8 block_num, u16 lines) {
	struct block_data header = {
		.data_len = cpu_to_le32(len),
		.color = color,
		.block_num = block_num,
		.lines = cpu_to_le16(lines),
	};
	write_block(M2X00W_BLOCK_DATA, &header, sizeof(header), stream);
	fwrite(buf + buf_pos - len, 1, len, stream);
//	DBG("wrote %d byte block, buf_pos=%d\n", block_len, buf_pos);
}

void encode_color(cups_raster_t *ras, FILE *stream, int line_len_file, u16 lines_per_block, enum m2x00w_color color) {
	int line = 0, block_len = 0;
	u8 data[line_len_file];
	u8 blocks = 0;

	while (cupsRasterReadPixels(ras, data, line_len_file)) {
		block_len += encode_line(data, line_len_file);
		line++;
		if (line % lines_per_block == 0) {
			write_data_block(stream, color, buf, block_len, ++blocks, lines_per_block);
			block_len = 0;
		}
	}
	if (line % lines_per_block)
		write_data_block(stream, color, buf, block_len, ++blocks, line % lines_per_block);
}

char *ppd_get(ppd_file_t *ppd, const char *name) {
	ppd_attr_t *attr = ppdFindAttr(ppd, name, NULL);

	if (attr)
		return attr->value;
	else {
		ppd_choice_t *choice;
		choice = ppdFindMarkedChoice(ppd, name);
		if (!choice)
			return NULL;
		return choice->choice;
	}
}

int main(int argc, char *argv[]) {
	cups_raster_t *ras = NULL;
	cups_page_header2_t page_header;
	unsigned int page = 0, copies;
	int fd;
	ppd_file_t *ppd;
	bool header_written = false;

	if (argc < 6 || argc > 7) {
		fprintf(stderr, "usage: rastertom2x00w job-id user title copies options [file]\n");
		return 1;
	}
	int n;
	cups_option_t *options;

	copies = atoi(argv[4]);
	if (copies < 1)
		copies = 1;

	if (argc > 6) {
		fd = open(argv[6], O_RDONLY);
		if (fd == -1) {
			perror("ERROR: Unable to open raster file - ");
			return 1;
		}
	} else
		fd = 0;
	ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
	ppd = ppdOpenFile(getenv("PPD"));
	if (!ppd) {
		fprintf(stderr, "Unable to open PPD file %s\n", getenv("PPD"));
		return 2;
	}
	ppdMarkDefaults(ppd);
	n = cupsParseOptions(argv[5], 0, &options);
	cupsMarkOptions(ppd, n, options);
	cupsFreeOptions(n, options);

	model = atoi(ppd_get(ppd, "cupsModelNumber"));
	DBG("model=0x%02x", model);
	if (model != M2300W && model != M2400W && model != M2500W) {
		ERR("Invalid model number 0x%02x\n", model);
		return 3;
	}

	/* document beginning */
	struct block_begin begin = { .model = model, .color = 0x10 };
	write_block(M2X00W_BLOCK_BEGIN, &begin, sizeof(begin), stdout);

	while (cupsRasterReadHeader2(ras, &page_header)) {
		page++;
		fprintf(stderr, "PAGE: %d %d\n", page, page_header.NumCopies);

		line_len_file = page_header.cupsBytesPerLine;
//		line_len = ROUND_UP_MULTIPLE(line_len_file, 4);
		height = page_header.cupsHeight;
		width = page_header.cupsWidth;
		u16 lines_per_block = DIV_ROUND_UP(height, BLOCKS_PER_PAGE);
		buf_size = line_len_file * lines_per_block;////////////could be bigger in worst case?!
		buf = realloc(buf, buf_size);
		buf_pos = 0;
		dpi = page_header.HWResolution[0];
		DBG("line_len_file=%d, height=%d width=%d", line_len_file, height, width);
		DBG("dpi_x=%d,cupsColorOrder=%d,cupsColorSpace=%d", dpi, page_header.cupsColorOrder, page_header.cupsColorSpace);
		if (!header_written) {	/* print parameters */
			struct block_params params = { .res_y = (model == M2300W) ? RES_1200DPI : RES_600DPI };
			if (dpi == 600)
				params.res_x = RES_MULT1;
			else if (dpi == 1200)
				params.res_x = RES_MULT2;
			else if (dpi == 2400)
				params.res_x = RES_MULT4;
			write_block(M2X00W_BLOCK_PARAMS, &params, sizeof(params), stdout);
			header_written = true;
		}
		char *page_size_name = page_header.cupsPageSizeName;
		/* get page size name from PPD if cupsPageSizeName is empty */
		if (strlen(page_size_name) == 0)
			page_size_name = ppd_get(ppd, "PageSize");

		struct block_page page_params = {
			.copies = (model == M2500W) ? copies : 1,
			.x_end = cpu_to_le16(width),
			.y_end = cpu_to_le16(height),
			.paper_size = encode_paper_size(page_size_name),
//			.custom_width = ,
//			.custom_height = ,
//			.duplex = ,
			.paper_weight = page_header.cupsMediaType,
			.unknown = (model == M2300W) ? 1 : 0,/////
		};

		if (page_header.cupsColorSpace == CUPS_CSPACE_K) {
			page_params.color_mode = (model == M2300W) ? MODE_BW_2300 : MODE_BW;
			page_params.blocks1 = page_params.blocks2 = cpu_to_le16(BLOCKS_PER_PAGE);
		} else if (page_header.cupsColorSpace == CUPS_CSPACE_YMCK) {
			page_params.color_mode = MODE_COLOR;
			page_params.blocks1 = page_params.blocks2 = cpu_to_le16(BLOCKS_PER_PAGE * 4);
		} else {
			ERR("invalid color space: %d", page_header.cupsColorSpace);
			return 3;
		}
		write_block(M2X00W_BLOCK_PAGE, &page_params, sizeof(page_params), stdout);
		/* read raster data */
		if (page_params.color_mode == MODE_COLOR) {
			encode_color(ras, stdout, line_len_file, lines_per_block, COLOR_Y);
			encode_color(ras, stdout, line_len_file, lines_per_block, COLOR_M);
			encode_color(ras, stdout, line_len_file, lines_per_block, COLOR_C);
		}
		encode_color(ras, stdout, line_len_file, lines_per_block, COLOR_K);
	}
	free(buf);
	ppdClose(ppd);
	cupsRasterClose(ras);
	/* end of print data */
	char zero = 0;
	write_block(M2X00W_BLOCK_ENDPART, &zero, 1, stdout);
	/* end of document */
	write_block(M2X00W_BLOCK_END, &zero, 1, stdout);

	return 0;
}
