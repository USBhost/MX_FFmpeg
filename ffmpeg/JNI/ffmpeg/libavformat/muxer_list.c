static const AVOutputFormat * const muxer_list[] = {
    &ff_dash_muxer,
    &ff_mxv_muxer,
    &ff_mov_muxer,
    &ff_mp3_muxer,
    &ff_mp4_muxer,
    &ff_srt_muxer,
    &ff_webvtt_muxer,
    NULL };
