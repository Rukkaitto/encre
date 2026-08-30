/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/*----------------------------------------------*/
/* --- The one vendored TJpgDec file this project EDITS -----------------------
/
/ tjpgd.c and tjpgd.h are upstream byte for byte; every choice this project makes
/ about the decoder is one of the five macros below. Two of the five differ from
/ the stock values, and both are marked CHANGED with the reason -- a version bump
/ replaces the other two files outright and has to re-apply only this one.
/--------------------------------------------------------------------------- */

#define	JD_SZBUF		512
/* Specifies size of stream input buffer.
/  Stock. It comes out of the pool jd_prepare is handed, and it also bounds the
/  largest JPEG segment the decoder will accept (a longer DHT or DQT is
/  JDR_MEM2) -- 512 is what upstream sizes that limit at and no corpus cover has
/  needed more.
*/

#define JD_FORMAT		2
/* Specifies output pixel format.
/  0: RGB888 (24-bit/pix)
/  1: RGB565 (16-bit/pix)
/  2: Grayscale (8-bit/pix)
/
/  CHANGED from 0. The panel is grey and CoverFitter wants grey, so decoding to
/  RGB would allocate a three-times-larger MCU rectangle and then average it
/  away. Format 2 takes the Y component straight out of the MCU buffer and never
/  touches Cb/Cr -- so it is also the cheaper path, not a conversion bolted on.
*/

#define	JD_USE_SCALE	1
/* Switches output descaling feature.
/  0: Disable
/  1: Enable
/
/  Stock, and load-bearing: the free 1/2, 1/4 and 1/8 out of the IDCT is what
/  makes a 2.94 MP cover affordable at all. JpegDecoder::decode picks the
/  divisor from the caller's smallest usable output.
*/

#define JD_TBLCLIP		1
/* Use table conversion for saturation arithmetic. A bit faster, but increases 1 KB of code size.
/  0: Disable
/  1: Enable
/
/  Stock. ~1 KB of flash against a 6.25 MB app partition.
*/

#define JD_FASTDECODE	0
/* Optimization level
/  0: Basic optimization. Suitable for 8/16-bit MCUs.
/  1: + 32-bit barrel shifter. Suitable for 32-bit MCUs.
/  2: + Table conversion for huffman decoding (wants 6 << HUFF_BIT bytes of RAM)
/
/  STOCK, AND IT HAS TO STAY STOCK WHILE JD_FORMAT IS 2. Level 1 looks like the
/  obvious pick -- the C3 is a 32-bit RISC-V, and this file was planned with it
/  taken -- and it is wrong here, for a reason that is upstream's rather than
/  ours:
/
/    Level >= 1 widens jd_yuv_t to int16_t and DEFERS the saturation clamp out
/    of block_idct (tjpgd.c: the `#if JD_FASTDECODE >= 1` arm stores a raw
/    `(int16_t)(v >> 8)` where the else arm stores `BYTECLIP(v >> 8)`).
/    mcu_output re-applies BYTECLIP on the RGB path -- and NOT on the
/    monochrome one, which is a plain `*pix++ = (uint8_t)*py++`. So at
/    JD_FORMAT 2 an out-of-range Y WRAPS modulo 256 instead of clamping, and a
/    sample one below black comes out one below WHITE.
/
/  Measured against stb_image over test/unit/fixtures/images/baseline.jpg: level
/  1 puts 30 pixels of 740,000 up to 255 grey levels wrong -- isolated white
/  specks through the dark half of the picture, which is exactly the salt-and-
/  pepper a cover must not have. Level 0's worst pixel is out by 1.
/
/  AND IT BUYS NOTHING MEASURABLE. Best of twelve full decodes of that fixture,
/  desktop -O2: level 0 at 21.65 ms, level 1 at 22.05 ms. The barrel-shifter
/  path was not faster even before its cost was counted.
/
/  If level 1 is ever wanted on the device, the fix is a BYTECLIP in
/  mcu_output's two monochrome arms -- a patch to tjpgd.c, which this project
/  has precedent for (third_party/stb_truetype.h) and no reason to spend here.
/  Level 2 additionally wants 6 KB of pool for its Huffman LUTs, against a band
/  buffer that is already 8-17 KB on a 42 KB reading floor.
*/
