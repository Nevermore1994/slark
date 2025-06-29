package com.slark.demo.ui

data class PlayUrl(val name: String, val url: String) {
    companion object {
        val urls = arrayOf (
            PlayUrl("Sintel trailer mp4", "https://media.w3.org/2010/05/sintel/trailer.mp4"),
            PlayUrl("海洋 mp4", "http://vjs.zencdn.net/v/oceans.mp4"),
            PlayUrl("花 mp4", "https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4"),
            PlayUrl("西瓜视频 m3u8", "https://sf1-cdn-tos.huoshanstatic.com/obj/media-fe/xgplayer_doc_video/hls/xgplayer-demo.m3u8"),
            PlayUrl("定时器 m3u8", "http://devimages.apple.com/iphone/samples/bipbop/gear3/prog_index.m3u8"),
            PlayUrl("钢铁之泪 m3u8", "https://demo.unified-streaming.com/k8s/features/stable/video/tears-of-steel/tears-of-steel.ism/.m3u8"),
            PlayUrl("直播 m3u8", "http://kbs-dokdo.gscdn.com/dokdo_300/_definst_/dokdo_300.stream/playlist.m3u8")
        )
    }
}

