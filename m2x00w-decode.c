/* CUPS driver for Minolta magicolor 2300W/2400W/2500W printers - decoder */
/* Copyright (c) 2014 Ondrej Zary */
/* Based on min_decode by Orion Sky Lawlor, olawlor@acm.org */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m2x00w.h"

enum m2x00w_model model;
int line_bytes;
u8 *line_buf;

char *decode_model(u8 model) {
	switch (model) {
	case 0x81: return "1200W/1250W";
	case 0x82: return "2300W";
	case 0x83: return "1300W/1350W";
	case 0x85: return "2400W";
	case 0x87: return "2500W";
	default:   return "unknown";
	}
}

char *decode_media_type(u8 media) {
	static char *media_types[] = { "Plain", "Thick", "Transparency", "Envelope", "Letterhead",
				    "Postcard", "Label", "unknown", "GLOSSY/Glossy" };
	if (media < ARRAY_SIZE(media_types))
		return media_types[media];
	return "unknown";
}

char *decode_paper_size(u8 paper) {
	switch (paper) {
	case 0x04: return "A4";
	case 0x06: return "B5 (JIS)";
	case 0x08: return "A5";
	case 0x0C: return "J postcard";
	case 0x0D: return "double postcard";
	case 0x0F: return "envelope You #4";
	case 0x12: return "folio";
	case 0x15: return "Kai-32";
	case 0x19: return "legal";
	case 0x1A: return "G.legal";
	case 0x1B: return "letter";
	case 0x1D: return "G.letter";
	case 0x1F: return "executive";
	case 0x21: return "statement";
	case 0x24: return "envelope monarch";
	case 0x25: return "envelope COM10";
	case 0x26: return "envelope DL";
	case 0x27: return "envelope C5";
	case 0x28: return "envelope C6";
	case 0x29: return "B5 (ISO)";
	case 0x2D: return "envelope Chou #3";
	case 0x2E: return "envelope Chou #4";
	case 0x31: return "CUSTOM";
	case 0x46: return "foolscap";
	case 0x51: return "16K";
	case 0x52: return "Kai-16";
	case 0x53: return "letter plus";
	case 0x54: return "UK quarto";
	case 0x65: return "photo";
	default:   return "unknown";
	}
}

void output_byte(FILE *fout, u8 b) {
	static int buf_pos;

	if (model == M2400W)	/* interleaved lines */
		line_buf[buf_pos % 2 * line_bytes + buf_pos / 2] = b;
	else
		line_buf[buf_pos] = b;
	buf_pos++;
	if (buf_pos >= 2 * line_bytes) {
		fwrite(line_buf, 1, 2 * line_bytes, fout);
		buf_pos = 0;
	}
}

void output_rep(FILE *fout, u8 byte, int count)
{
	for (int i = 0; i < count; i++)
		output_byte(fout, byte);
}

void decode_begin_block(void *data) {
	struct block_begin *begin = data;

	model = begin->model;
	printf("Printer model: %s\n", decode_model(model));
}

void decode_params_block(void *data) {
	struct block_params *params = data;
	int dpi_x, dpi_y = 600;

	if (params->res_y != RES_600DPI || (params->res_y == RES_1200DPI && model != M2300W))
		fprintf(stderr, "Invalid vertical resolution: 0x%02hhx\n", params->res_y);
	switch (params->res_x) {
	case RES_MULT1:
		dpi_x = dpi_y;
		break;
	case RES_MULT2:
		dpi_x = 2 * dpi_y;
		break;
	case RES_MULT4:
		dpi_x = 4 * dpi_y;
		break;
	default:
		dpi_x = 0;
		fprintf(stderr, "Invalid X resolution multiplier: 0x%02hhx\n", params->res_x);
	}
	printf("Print parameters: resolution %dx%d dpi\n", dpi_x, dpi_y);
}

void decode_page_block(void *data, FILE *fout) {
	struct block_page *page = data;
	int page_width = le16_to_cpu(page->x_end);
	int page_height = le16_to_cpu(page->y_end);
	int nbands = (page->color_mode == MODE_COLOR) ? 4 : 1;

	line_bytes = DIV_ROUND_UP(page_width, 8);
	printf("Page parameters: paper %x (%s), size %d x %d pixels\n",
		page->paper_size, decode_paper_size(page->paper_size), page_width, page_height);////

	fseek(fout, SEEK_SET, 0);
	fprintf(fout, "P4\n%d %d\n", page_width, page_height * nbands);
	line_buf = realloc(line_buf, 2 * line_bytes);
}

void decode_data_block(void *data, FILE *f, FILE *fout) {
	struct block_data *header = data;
	int nbytes = le32_to_cpu(header->data_len);
	int lines = le16_to_cpu(header->lines);
	int line_bytes_virt = line_bytes;

	printf("  Raster data: ch%d, #%d, %d bytes compressed, %d uncompressed lines are %d bytes each\n",
		header->color, header->block_num, nbytes, lines, line_bytes_virt);

	if (model == M2400W) {
		lines /= 2;
		line_bytes_virt *= 2;
	}
	for (int line = 0; line < lines; line++) {
		u8 table[16];
		u8 table_len;

		printf("POS=0x%lx: ", ftell(f));
		fread(&table_len, 1, 1, f);
		printf("table_len=0x%02hhx\n", table_len);
		if (!(table_len & 0x80))
			fprintf(stderr, "Invalid line start byte 0x%02hhx!\n", table_len);
		if (table_len & 0x40) { /* 4-byte row length padding (2500W) */
			if (model != M2500W)
				fprintf(stderr, "2500W padding present but printer is not 2500W!\n");
			fread(table, 1, 2, f);
			int pad_len = table[0] >> 6; /* 0, 1, 2 or 3 bytes */
			int row_len = ((table[0] & 0x3f) << 8) | table[1];
			printf("row size: %d, reading %d padding bytes\n", row_len, pad_len);
			fread(table, 1, pad_len, f);
		} else
			if (model == M2500W)
				fprintf(stderr, "2500W padding missing!\n");
		table_len &= 0x3f;
		if (table_len > 16) {
			fprintf(stderr, "Table too big: %d bytes!\n", table_len);
			return;
		}
		fread(table, 1, table_len, f);
		printf("table: ");
		for (int i = 0; i < table_len; i++)
			printf("%02hhx ", table[i]);
		printf("\n");
		int pos = 0;
		while (pos < line_bytes_virt) {
			u8 b = fgetc(f);
			int count = b & 0x3f;
			switch (b & 0xc0) {
			case 0xc0: /* long repeated bytes */
				count <<= 6;
				/* fall through */
			case 0x80: /* short repeated bytes */
				if (count == 0)
					fprintf(stderr, "zero repeat count!");
				u8 byte = fgetc(f);
				printf("%s repeat: %d-times 0x%02hhx\n", (b >= 0xc0) ? "long" : "short", count, byte);
				output_rep(fout, byte, count);
				pos += count;
				break;
			case 0x40: /* table */
				printf("%d bytes from table\n", 2 * (count + 1));
				for (int i = 0; i < count + 1; i++) {
					u8 idx = fgetc(f);
					printf("table %d:0x%02hhx %d:0x%02hhx\n", (idx >> 4) & 0x0f, table[(idx >> 4) & 0x0f], idx & 0x0f, table[idx & 0x0f]);
					output_byte(fout, table[(idx >> 4) & 0x0f]);
					output_byte(fout, table[idx & 0x0f]);
				}
				pos += 2 * (count + 1);
				break;
			case 0x00: /* uncompressed bytes */
				printf("uncompressed %d bytes: ", count + 1);
				for (int i = 0; i < count + 1; i++) {
					u8 byte = fgetc(f);
					printf("%02hhx ", byte);
					output_byte(fout, byte);
				}
				pos += count + 1;
				printf("\n");
				break;
			}
		}
		if (pos != line_bytes_virt) {
			fprintf(stderr, "Wrong line length %d!\n", pos);
			return;
		}
	}
}

void min_parse_block(FILE *f, FILE *fout)
{
	struct header header;
	u8 sum, sum_header;
	u8 *data;

	fread(&header, 1, sizeof(header), f);
	int len = le16_to_cpu(header.len);

	if (feof(f))
		return;

	if (header.magic != M2X00W_MAGIC)
		fprintf(stderr, "Invalid block magic byte 0x%02hhx!\n", header.magic);
	printf("Block type 0x%02hhx: seq %d, length %d\n", header.type, header.seq, len);
	if (header.type != (header.type_inv ^ 0xff))
		fprintf(stderr, "Invalid inverted block type byte 0x%02hhx!\n", header.type_inv);

	data = malloc(len);
	fread(data, 1, len, f);
	printf("  Data: ");
	for (int i = 0; i < len; i++)
		printf("%02hhx ", data[i]);
	printf("\n");

	fread(&sum_header, 1, 1, f);
	sum = checksum(&header, sizeof(header)) + checksum(data, len);
	if (sum_header != sum)
		fprintf(stderr, "Incorrect checksum 0x%02hhx, should be 0x%02hhx!\n", sum_header, sum);

	switch (header.type) {
	case M2X00W_BLOCK_BEGIN:
		decode_begin_block(data);
		break;
	case M2X00W_BLOCK_PARAMS:
		decode_params_block(data);
		break;
	case M2X00W_BLOCK_PAGE:
		decode_page_block(data, fout);
		break;
	case M2X00W_BLOCK_DATA:
		decode_data_block(data, f, fout);
		break;
	case M2X00W_BLOCK_ENDPART:
		break;
	case M2X00W_BLOCK_END:
		break;
	default:
		printf("Unknown block type 0x%02hhx\n", header.type);
		exit(1);
	}
	printf("\n");
	free(data);
}

void usage() {
	printf("usage: m2x00w-decode <file.prn> <outfile.pbm>\n");
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		usage();
		return 1;
	}

	FILE *f = fopen(argv[1], "r");
	if (!f) {
		perror("Unable to open file");
		return 2;
	}
	FILE *fout = fopen(argv[2], "w");
	if (!fout) {
		perror("Unable to open output file");
		return 2;
	}

	while (!feof(f))
		min_parse_block(f, fout);

	printf("End of file reached\n");

	free(line_buf);
	fclose(fout);
	fclose(f);
	return 0;
}
