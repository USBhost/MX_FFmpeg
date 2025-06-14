# From http://developer.samsung.com/android/technical-docs/Porting-and-Using-the-Speex-Library-in-Android-with-JNI
# From https://github.com/haxar/mangler/blob/master/android/jni/Android.mk

MY_DIR := $(call my-dir)

LOCAL_PATH := $(MY_DIR)/openssl-1.0.2s

# ssl
include $(CLEAR_VARS)
LOCAL_MODULE    := ssl
LOCAL_SRC_FILES := libssl.a
include $(PREBUILT_STATIC_LIBRARY)

# crypto
include $(CLEAR_VARS)
LOCAL_MODULE    := crypto
LOCAL_SRC_FILES := libcrypto.a
include $(PREBUILT_STATIC_LIBRARY)

# # libsmb2
# include $(CLEAR_VARS)
# LOCAL_PATH  := $(MY_DIR)/libsmb2/lib/.libs
# LOCAL_MODULE    := smb2
# LOCAL_SRC_FILES := libsmb2.a
# include $(PREBUILT_STATIC_LIBRARY)

# libdav1d
include $(CLEAR_VARS)
LOCAL_PATH  := $(MY_DIR)/dav1d/builddir/$(TARGET_ARCH_ABI)/src
LOCAL_MODULE    := dav1d
LOCAL_SRC_FILES := libdav1d.a
include $(PREBUILT_STATIC_LIBRARY)

#
# Opus
#
include $(CLEAR_VARS)
LOCAL_PATH  := $(MY_DIR)/opus-1.1

LOCAL_SRC_FILES := \
celt/bands.c \
celt/celt.c \
celt/celt_decoder.c \
celt/celt_encoder.c \
celt/cwrs.c \
celt/entcode.c \
celt/entdec.c \
celt/entenc.c \
celt/kiss_fft.c \
celt/laplace.c \
celt/mathops.c \
celt/mdct.c \
celt/modes.c \
celt/pitch.c \
celt/celt_lpc.c \
celt/quant_bands.c \
celt/rate.c \
celt/vq.c \
silk/CNG.c \
silk/code_signs.c \
silk/init_decoder.c \
silk/decode_core.c \
silk/decode_frame.c \
silk/decode_parameters.c \
silk/decode_indices.c \
silk/decode_pulses.c \
silk/decoder_set_fs.c \
silk/dec_API.c \
silk/encode_indices.c \
silk/encode_pulses.c \
silk/gain_quant.c \
silk/interpolate.c \
silk/LP_variable_cutoff.c \
silk/NLSF_decode.c \
silk/NSQ.c \
silk/NSQ_del_dec.c \
silk/PLC.c \
silk/shell_coder.c \
silk/tables_gain.c \
silk/tables_LTP.c \
silk/tables_NLSF_CB_NB_MB.c \
silk/tables_NLSF_CB_WB.c \
silk/tables_other.c \
silk/tables_pitch_lag.c \
silk/tables_pulses_per_block.c \
silk/VAD.c \
silk/control_audio_bandwidth.c \
silk/quant_LTP_gains.c \
silk/VQ_WMat_EC.c \
silk/HP_variable_cutoff.c \
silk/NLSF_encode.c \
silk/NLSF_VQ.c \
silk/NLSF_unpack.c \
silk/NLSF_del_dec_quant.c \
silk/process_NLSFs.c \
silk/stereo_LR_to_MS.c \
silk/stereo_MS_to_LR.c \
silk/check_control_input.c \
silk/control_SNR.c \
silk/control_codec.c \
silk/A2NLSF.c \
silk/ana_filt_bank_1.c \
silk/biquad_alt.c \
silk/bwexpander_32.c \
silk/bwexpander.c \
silk/decode_pitch.c \
silk/inner_prod_aligned.c \
silk/lin2log.c \
silk/log2lin.c \
silk/LPC_analysis_filter.c \
silk/LPC_inv_pred_gain.c \
silk/table_LSF_cos.c \
silk/NLSF2A.c \
silk/NLSF_stabilize.c \
silk/NLSF_VQ_weights_laroia.c \
silk/pitch_est_tables.c \
silk/resampler.c \
silk/resampler_down2_3.c \
silk/resampler_down2.c \
silk/resampler_private_AR2.c \
silk/resampler_private_down_FIR.c \
silk/resampler_private_IIR_FIR.c \
silk/resampler_private_up2_HQ.c \
silk/resampler_rom.c \
silk/sigm_Q15.c \
silk/sort.c \
silk/sum_sqr_shift.c \
silk/stereo_decode_pred.c \
silk/stereo_find_predictor.c \
silk/stereo_quant_pred.c \
silk/fixed/LTP_analysis_filter_FIX.c \
silk/fixed/LTP_scale_ctrl_FIX.c \
silk/fixed/corrMatrix_FIX.c \
silk/fixed/find_LPC_FIX.c \
silk/fixed/find_LTP_FIX.c \
silk/fixed/find_pitch_lags_FIX.c \
silk/fixed/find_pred_coefs_FIX.c \
silk/fixed/noise_shape_analysis_FIX.c \
silk/fixed/prefilter_FIX.c \
silk/fixed/process_gains_FIX.c \
silk/fixed/regularize_correlations_FIX.c \
silk/fixed/residual_energy16_FIX.c \
silk/fixed/residual_energy_FIX.c \
silk/fixed/solve_LS_FIX.c \
silk/fixed/warped_autocorrelation_FIX.c \
silk/fixed/apply_sine_window_FIX.c \
silk/fixed/autocorr_FIX.c \
silk/fixed/burg_modified_FIX.c \
silk/fixed/k2a_FIX.c \
silk/fixed/k2a_Q16_FIX.c \
silk/fixed/pitch_analysis_core_FIX.c \
silk/fixed/vector_ops_FIX.c \
silk/fixed/schur64_FIX.c \
silk/fixed/schur_FIX.c \
src/opus.c \
src/opus_decoder.c \
src/opus_multistream.c \
src/opus_multistream_decoder.c

LOCAL_CFLAGS := -I$(LOCAL_PATH)/include -I$(LOCAL_PATH)/celt -I$(LOCAL_PATH)/silk -I$(LOCAL_PATH)/silk/fixed -Drestrict='' -D__EMX__ -DOPUS_BUILD -DFIXED_POINT -DUSE_ALLOCA -DHAVE_LRINT -DHAVE_LRINTF -O3 -fno-math-errno

include $(MY_DIR)/a-arch-$(TARGET_ARCH).mk

LOCAL_MODULE    := opus
include $(BUILD_STATIC_LIBRARY)


#
# Speex
#
LOCAL_PATH := $(MY_DIR)/speex-1.2rc1

include $(CLEAR_VARS)

LOCAL_CFLAGS = -DFIXED_POINT -DUSE_KISS_FFT -DEXPORT="" -UHAVE_CONFIG_H

# Clang only.
ifneq ($(findstring clang, $(NDK_TOOLCHAIN_VERSION)),)
    LOCAL_CFLAGS += \
    -Wno-literal-conversion
endif


LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

LOCAL_SRC_FILES :=  \
./libspeex/bits.c \
./libspeex/buffer.c \
./libspeex/cb_search.c \
./libspeex/exc_10_16_table.c \
./libspeex/exc_10_32_table.c \
./libspeex/exc_20_32_table.c \
./libspeex/exc_5_256_table.c \
./libspeex/exc_5_64_table.c \
./libspeex/exc_8_128_table.c \
./libspeex/fftwrap.c \
./libspeex/filterbank.c \
./libspeex/filters.c \
./libspeex/gain_table.c \
./libspeex/gain_table_lbr.c \
./libspeex/hexc_10_32_table.c \
./libspeex/hexc_table.c \
./libspeex/high_lsp_tables.c \
./libspeex/jitter.c \
./libspeex/kiss_fft.c \
./libspeex/kiss_fftr.c \
./libspeex/lpc.c \
./libspeex/lsp.c \
./libspeex/lsp_tables_nb.c \
./libspeex/ltp.c \
./libspeex/mdf.c \
./libspeex/modes.c \
./libspeex/modes_wb.c \
./libspeex/nb_celp.c \
./libspeex/preprocess.c \
./libspeex/quant_lsp.c \
./libspeex/resample.c \
./libspeex/sb_celp.c \
./libspeex/scal.c \
./libspeex/smallft.c \
./libspeex/speex.c \
./libspeex/speex_callbacks.c \
./libspeex/speex_header.c \
./libspeex/stereo.c \
./libspeex/vbr.c \
./libspeex/vq.c \
./libspeex/window.c

include $(MY_DIR)/a-arch-$(TARGET_ARCH).mk

LOCAL_MODULE := speex
include $(BUILD_STATIC_LIBRARY)

#
# libmodplug
# used for supporting some audio formats.
# it is under public domain.
#

LOCAL_PATH := $(MY_DIR)/libmodplug/src

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
$(LOCAL_PATH) \
$(LOCAL_PATH)/libmodplug

LOCAL_SRC_FILES := \
fastmix.cpp \
load_669.cpp \
load_abc.cpp \
load_amf.cpp \
load_ams.cpp \
load_dbm.cpp \
load_dmf.cpp \
load_dsm.cpp \
load_far.cpp \
load_it.cpp \
load_j2b.cpp \
load_mdl.cpp \
load_med.cpp \
load_mid.cpp \
load_mod.cpp \
load_mt2.cpp \
load_mtm.cpp \
load_okt.cpp \
load_pat.cpp \
load_psm.cpp \
load_ptm.cpp \
load_s3m.cpp \
load_stm.cpp \
load_ult.cpp \
load_umx.cpp \
load_wav.cpp \
load_xm.cpp \
mmcmp.cpp \
modplug.cpp \
snd_dsp.cpp \
sndfile.cpp \
snd_flt.cpp \
snd_fx.cpp \
sndmix.cpp

LOCAL_CFLAGS := \
-DPIC \
-DMODPLUG_BUILD=1 \
-DHAVE_SETENV \
-DHAVE_SINF \
-Wno-unused-but-set-variable

#TODO:
#It may be a risk to diable follwing flags roughly.
#More is to be done to investigate this issue
#when effort and schedule available.
#LOCAL_CFLAGS := \
-DPIC \
-DHAVE_CONFIG_H \
-DMODPLUG_BUILD=1 \
-Wno-unused-but-set-variable

# Clang only.
ifneq ($(findstring clang, $(NDK_TOOLCHAIN_VERSION)),)
    LOCAL_CFLAGS += \
    -Wno-unknown-warning-option \
    -Wno-unused-function
endif

ifeq ($(VERBOSE_MODE),1)
	LOCAL_CFLAGS += -DVERBOSE_MODE
endif


LOCAL_CPPFLAGS := \
-std=gnu++11 \
-g \
-fno-exceptions \
-Wall \
-ffast-math \
-fno-common \
-D_REENTRANT \
-DSYM_VISIBILITY \
-fvisibility=hidden \
-Wno-sign-compare

include $(MY_DIR)/a-arch-$(TARGET_ARCH).mk

LOCAL_MODULE := modplug
include $(BUILD_STATIC_LIBRARY)

#
# libxml2
#
LOCAL_PATH := $(MY_DIR)/libxml2

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
SAX.c              \
entities.c         \
encoding.c         \
error.c            \
parserInternals.c  \
parser.c           \
tree.c             \
hash.c             \
list.c             \
xmlIO.c            \
xmlmemory.c        \
uri.c              \
valid.c            \
xlink.c            \
HTMLparser.c       \
HTMLtree.c         \
debugXML.c         \
xpath.c            \
xpointer.c         \
xinclude.c         \
DOCBparser.c       \
catalog.c          \
globals.c          \
threads.c          \
c14n.c             \
xmlstring.c        \
buf.c              \
xmlregexp.c        \
xmlschemas.c       \
xmlschemastypes.c  \
xmlunicode.c       \
xmlreader.c        \
relaxng.c          \
dict.c             \
SAX2.c             \
xmlwriter.c        \
legacy.c           \
chvalid.c          \
pattern.c          \
xmlsave.c          \
xmlmodule.c        \
schematron.c

LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_CFLAGS := -DLIBXML_THREAD_ENABLED=1         \
                -Wno-missing-field-initializers   \
                -Wno-self-assign                  \
                -Wno-sign-compare                 \
                -Wno-tautological-pointer-compare
LOCAL_MODULE := xml2
include $(BUILD_STATIC_LIBRARY)


# libmp3lame
# LOCAL_PATH := $(MY_DIR)/lame-3.100
# include $(CLEAR_VARS)
# LOCAL_MODULE    := mp3lame
# LOCAL_SRC_FILES := libmp3lame.a
# include $(PREBUILT_STATIC_LIBRARY)


#
# My own.
#
LOCAL_PATH := $(MY_DIR)/modified_src

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
ffmpeg/dvdsubdec3.c \
iconv/iconv_wrap.c \
mxv/mxv_wrap.c \
mxd/mxd_wrap.c \
usb/usb_wrap.c \
../mxutil.refs.c \
download/downloadhttp_wrap.c \
sort.c

# v5, v6(+vfp)를 제외하고는 Android M의 OpenSSL 제거에 대응하기 위해 mxutil에 wrapper를 넣는다.
#ifneq ($(ARCH),$(filter $(ARCH),armv5te armv6))
#   LOCAL_SRC_FILES += openssl/openssl_wrap.c
#endif

LOCAL_CFLAGS := \
-D_LARGEFILE_SOURCE \
-DPIC \
-DFF_API_AVPICTURE=1

LOCAL_LDLIBS :=
LOCAL_LDFLAGS := -Wl,--version-script=$(MY_DIR)/version_scripts/mxutil \


#-L$(NDK_APP_DST_DIR)
#-lsmb2


include $(MY_DIR)/a-arch-$(TARGET_ARCH).mk
LOCAL_ASFLAGS := $(LOCAL_CFLAGS)
ifeq ($(TARGET_ARCH),arm)
LOCAL_STATIC_LIBRARIES := opus speex modplug ssl crypto xml2 dav1d
else ifeq ($(TARGET_ARCH),arm64)
LOCAL_STATIC_LIBRARIES := opus speex modplug ssl crypto xml2 dav1d
else
LOCAL_STATIC_LIBRARIES := opus speex modplug ssl crypto xml2 dav1d
endif
include $(BUILD_SHARED_LIBRARY)

