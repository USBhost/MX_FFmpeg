#include <math.h>
#include "nb_celp.h"
#include "lsp.h"
#include "ltp.h"
#include "quant_lsp.h"
#include "cb_search.h"
#include "filters.h"
#include "../include/speex/speex_bits.h"
#include "os_support.h"

#ifndef NULL
#define NULL 0
#endif

#define LSP_MARGIN .002f
#define SIG_SCALING  1.f
#define NB_DEC_BUFFER (NB_FRAME_SIZE+2*NB_PITCH_END+NB_SUBFRAME_SIZE+12)
#define NB_ORDER 10
#define NB_FRAME_SIZE 160
#define NB_SUBFRAME_SIZE 40
#define NB_NB_SUBFRAMES 4
#define NB_PITCH_START 17
#define NB_PITCH_END 144


struct speex_decode_state {
	float excBuf[NB_DEC_BUFFER]; /**< Excitation buffer */
	float *exc;                  /**< Start of excitation frame */
	float old_qlsp[10];          /**< Quantized LSPs for previous frame */
	float interp_qlpc[10];       /**< Interpolated quantized LPCs */
	float mem_sp[10];            /**< Filter memory for synthesis signal */
	int first;                   /**< Is this the first frame? */
};


static const float exc_gain_quant_scal1[2] = {0.70469f, 1.05127f};


struct speex_decode_state *nb_decoder_init(void)
{
	struct speex_decode_state *st;

	st = malloc(sizeof(*st));
	if (!st)
		return NULL;

	memset(st, 0, sizeof(*st));
	st->first = 1;

	return st;
}


void nb_decoder_destroy(struct speex_decode_state *state)
{
	if (state)
		free(state);
}


/* basic decoder using mode3 only */
int nb_decode(struct speex_decode_state *st, SpeexBits *bits, float *out)
{
	int i, sub, wideband, mode, qe;
	float ol_gain;
	float innov[NB_SUBFRAME_SIZE];
	float exc32[NB_SUBFRAME_SIZE];
	float qlsp[NB_ORDER], interp_qlsp[NB_ORDER];
	float ak[NB_ORDER];

	if (!bits)
		return -1;

	st->exc = st->excBuf + 2*NB_PITCH_END + NB_SUBFRAME_SIZE + 6;

	/* Decode Sub-modes */
	do {
		if (speex_bits_remaining(bits) < 5)
			return -1;

		wideband = speex_bits_unpack_unsigned(bits, 1);
		if (wideband) {
			printf("wideband not supported\n");
			return -2;
		}

		mode = speex_bits_unpack_unsigned(bits, 4);
		if (mode == 15)
			return -1;
      
	} while (mode > 8);

	if (mode != 3) {
		printf("only mode 3 supported\n");
		return -2;
	}

	/* Shift all buffers by one frame */
	SPEEX_MOVE(st->excBuf, st->excBuf+NB_FRAME_SIZE,
		   2*NB_PITCH_END + NB_SUBFRAME_SIZE + 12);

	/* Unquantize LSPs */
	lsp_unquant_lbr(qlsp, NB_ORDER, bits);

	/* Handle first frame */
	if (st->first) {
		st->first = 0;

		for (i=0; i<NB_ORDER; i++)
			st->old_qlsp[i] = qlsp[i];
	}
   
	/* Get global excitation gain */
	qe = speex_bits_unpack_unsigned(bits, 5);
	ol_gain = SIG_SCALING*exp(qe/3.5);

	/* Loop on subframes */
	for (sub=0; sub<4; sub++) {
		int offset, q_energy;
		float *exc, *sp;
		float ener;

		offset = NB_SUBFRAME_SIZE*sub;
		exc = st->exc + offset;
		sp = out + offset;

		SPEEX_MEMSET(exc, 0, NB_SUBFRAME_SIZE);

		/* Adaptive codebook contribution */
		pitch_unquant_3tap(exc, exc32, NB_PITCH_START,
				   NB_SUBFRAME_SIZE, bits, 0);

		sanitize_values32(exc32, -32000, 32000, NB_SUBFRAME_SIZE);
         
		/* Unquantize the innovation */
		SPEEX_MEMSET(innov, 0, NB_SUBFRAME_SIZE);

		/* Decode sub-frame gain correction */
		q_energy = speex_bits_unpack_unsigned(bits, 1);
		ener = exc_gain_quant_scal1[q_energy] * ol_gain;
                  
		/* Fixed codebook contribution */
		split_cb_shape_sign_unquant(innov, bits);

		/* De-normalize innovation and update excitation */
		signal_mul(innov, innov, ener, NB_SUBFRAME_SIZE);

		for (i=0; i<NB_SUBFRAME_SIZE; i++) {
			exc[i] = exc32[i] + innov[i];
		}
	}
   
	SPEEX_COPY(out, &st->exc[-NB_SUBFRAME_SIZE], NB_FRAME_SIZE);
   
	/* Loop on subframes */
	for (sub=0; sub<4; sub++) {
		const int offset = NB_SUBFRAME_SIZE*sub;
		float *sp, *exc;

		sp = out + offset;
		exc = st->exc + offset;

		/* LSP interpolation (quantized and unquantized) */
		lsp_interpolate(st->old_qlsp, qlsp, interp_qlsp, NB_ORDER,
				sub, NB_NB_SUBFRAMES, LSP_MARGIN);

		/* Compute interpolated LPCs (unquantized) */
		lsp_to_lpc(interp_qlsp, ak, NB_ORDER);

		iir_mem16(sp, st->interp_qlpc, sp, NB_SUBFRAME_SIZE,
			  NB_ORDER, st->mem_sp);

		/* Save for interpolation in next frame */
		for (i=0; i<NB_ORDER; i++)
			st->interp_qlpc[i] = ak[i];
	}
   
	/* Store the LSPs for interpolation in the next frame */
	for (i=0; i<NB_ORDER; i++)
		st->old_qlsp[i] = qlsp[i];

	return 0;
}
