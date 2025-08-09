package com.slark.demo.ui

data class PlayUrl(val name: String, val url: String) {
    companion object {
        val urls = arrayOf (
            PlayUrl("Sintel trailer mp4", "https://media.w3.org/2010/05/sintel/trailer.mp4"),
            PlayUrl("西瓜视频 mp4", "https://sf1-cdn-tos.huoshanstatic.com/obj/media-fe/xgplayer_doc_video/mp4/xgplayer-demo-360p.mp4"),
            PlayUrl("大灰熊 mp4", "https://www.w3schools.com/html/movie.mp4"),
            PlayUrl("新闻 mp4", "https://stream7.iqilu.com/10339/upload_transcode/202002/09/20200209105011F0zPoYzHry.mp4"),
            PlayUrl("西瓜视频 m3u8", "https://sf1-cdn-tos.huoshanstatic.com/obj/media-fe/xgplayer_doc_video/hls/xgplayer-demo.m3u8"),
            PlayUrl("定时器 m3u8", "http://devimages.apple.com/iphone/samples/bipbop/gear3/prog_index.m3u8"),
            PlayUrl("大兔子 m3u8", "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8"),
            PlayUrl("直播 m3u8", "http://kbs-dokdo.gscdn.com/dokdo_300/_definst_/dokdo_300.stream/playlist.m3u8")
        )
    }
}

