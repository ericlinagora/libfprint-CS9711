/*
 * Copyright (C) 2019 Synaptics Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "bmkt_internal.h"
#include "crc.h"

static const uint32_t Crc32Lookup16[16] =
{
 	 0x00000000,0x1DB71064,0x3B6E20C8,0x26D930AC,0x76DC4190,0x6B6B51F4,0x4DB26158,0x5005713C,
 	 0xEDB88320,0xF00F9344,0xD6D6A3E8,0xCB61B38C,0x9B64C2B0,0x86D3D2D4,0xA00AE278,0xBDBDF21C
};

uint32_t compute_crc32(uint8_t *data, uint8_t length, uint32_t prev_crc32)
{
	 uint32_t crc = ~prev_crc32;
	 const uint8_t* current = (const uint8_t*) data;

	 while (length-- != 0)
	 {
		 crc = Crc32Lookup16[(crc ^  *current      ) & 0x0F] ^ (crc >> 4);
		 crc = Crc32Lookup16[(crc ^ (*current >> 4)) & 0x0F] ^ (crc >> 4);
		 current++;
	 }

	 return ~crc; // same as crc ^ 0xFFFFFFFF
}

static const uint32_t crc_table[256] = {
    0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
    0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
    0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
    0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
    0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
    0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
    0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
    0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
    0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
    0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
    0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
    0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
    0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
    0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
    0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
    0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
    0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
    0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
    0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
    0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
    0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
    0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
    0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
    0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
    0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
    0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
    0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
    0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
    0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
    0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
    0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
    0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
    0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
    0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
    0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
    0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
    0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
    0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
    0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
    0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
    0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
    0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
    0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
    0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
    0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
    0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
    0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
    0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
    0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
    0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
    0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
    0x2d02ef8dL
};

/* Polynomial : 0x04c11db7 */
/*
 * A table of precomputed CRC values.
 * This table was computed using a polynomial of 0x[1]04c11b7
 * (which reversed is 0xedb88320.[8]).  This
 * is the standard CRC-32 polynomial used with HDLC, Ethernet, etc.
 * Note that this used the 'big endian' flag -e.
*/

static const uint32_t crc_poly2_table[256] = {
    0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,  /* [  0..  3] */
    0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,  /* [  4..  7] */
    0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,  /* [  8.. 11] */
    0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,  /* [ 12.. 15] */
    0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9,  /* [ 16.. 19] */
    0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,  /* [ 20.. 23] */
    0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011,  /* [ 24.. 27] */
    0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,  /* [ 28.. 31] */
    0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,  /* [ 32.. 35] */
    0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,  /* [ 36.. 39] */
    0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81,  /* [ 40.. 43] */
    0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,  /* [ 44.. 47] */
    0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49,  /* [ 48.. 51] */
    0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,  /* [ 52.. 55] */
    0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,  /* [ 56.. 59] */
    0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,  /* [ 60.. 63] */
    0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae,  /* [ 64.. 67] */
    0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,  /* [ 68.. 71] */
    0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,  /* [ 72.. 75] */
    0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,  /* [ 76.. 79] */
    0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,  /* [ 80.. 83] */
    0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,  /* [ 84.. 87] */
    0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066,  /* [ 88.. 91] */
    0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,  /* [ 92.. 95] */
    0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e,  /* [ 96.. 99] */
    0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,  /* [100..103] */
    0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,  /* [104..107] */
    0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,  /* [108..111] */
    0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,  /* [112..115] */
    0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,  /* [116..119] */
    0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686,  /* [120..123] */
    0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,  /* [124..127] */
    0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,  /* [128..131] */
    0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,  /* [132..135] */
    0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f,  /* [136..139] */
    0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,  /* [140..143] */
    0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47,  /* [144..147] */
    0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,  /* [148..151] */
    0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,  /* [152..155] */
    0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,  /* [156..159] */
    0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7,  /* [160..163] */
    0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,  /* [164..167] */
    0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f,  /* [168..171] */
    0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,  /* [172..175] */
    0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,  /* [176..179] */
    0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,  /* [180..183] */
    0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f,  /* [184..187] */
    0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,  /* [188..191] */
    0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,  /* [192..195] */
    0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,  /* [196..199] */
    0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,  /* [200..203] */
    0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,  /* [204..207] */
    0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30,  /* [208..211] */
    0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,  /* [212..215] */
    0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088,  /* [216..219] */
    0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,  /* [220..223] */
    0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,  /* [224..227] */
    0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,  /* [228..231] */
    0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,  /* [232..235] */
    0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,  /* [236..239] */
    0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0,  /* [240..243] */
    0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,  /* [244..247] */
    0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,  /* [248..251] */
    0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4,  /* [252..255] */
};

#define CRCDO1(msg) *checksum = crc_table[((int)*checksum ^ (*msg++)) & 0xff] ^ (*checksum >> 8);
#define CRCDO2(msg)  CRCDO1(msg); CRCDO1(msg);
#define CRCDO4(msg)  CRCDO2(msg); CRCDO2(msg);
#define CRCDO8(msg)  CRCDO4(msg); CRCDO4(msg);

int crc_checksum(uint32_t initialValue, uint32_t *checksum, uint8_t *msg, uint32_t len, uint32_t poly)
{
	int result = BMKT_SUCCESS;

	if (!checksum)
	{
		return BMKT_INVALID_PARAM;
	}

	*checksum = initialValue;

	switch (poly)
	{
		case CHECKSUM_CRC_POLY1:
		{
			*checksum = *checksum ^ 0xffffffffL;
			while (len >= 8)
			{
				CRCDO8(msg);
				len -= 8;
			}

			if (len)
			{
				do 
				{
					CRCDO1(msg);
				} while (--len);
			}

			*checksum = *checksum ^ 0xffffffffL;
			break;
		}

		case CHECKSUM_CRC_POLY2:
		{
			while (len != 0)
			{
				*checksum = crc_poly2_table[((*msg++ << 24) ^ (*checksum)) >> 24] ^ (*checksum << 8);
				len--;
			}

			*checksum = ~(*checksum);
			break;
		}

		default:
			result = BMKT_INVALID_PARAM;
			break;
	}

	return result;
}