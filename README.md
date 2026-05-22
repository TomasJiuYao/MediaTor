# MediaTor 2026/05/20
Using ffmpeg and rk_mpp to encoding and rtsp


v4l2_rkmpp_enc --push--> mediamtx:8554/live
  
--pull--> go2rtc:8555/live --WebRTC-->


![alt text](image.png)