WebRTC Reflector Server
===========
## Overview

This allows user to share their camera and microphone via the web page, and then see  
and hear themselves via the page. The purpose is to test their webcam, microphone,  
speakers, and network stability to a destination. The WebRTC web page should support  
Chrome, Edge, and Firefox.  

## Components
### Server
Backend, developed in C++, hosts index.html and respond to browser ```mediaEcho```  
```POST``` request.  
### Index.html
Frontend, records video from webcam and microphone and send the recorded  
fragment to server via ```POST```. From from response its fetches the fragment, play and  
update statistical data.   

## Build
Open VC++ Native Tool command prompt:  
```
cd webrtc_reflector
nmake
```

## Run
```
server.exe -i <IP Address> -p <Port>
```
Open a browser(Chrome/Firefox/Edge) and type: ```https://<IP Address>:<Port>```  
Since the self signed certificate used, the certificate verification fails, ignore  
and proceed to view the page. On the page, click on ```Connect``` button. The video  
streaming. On the upper left corner it shows following information:  
  
Container - Format used for video recording  
Bitrate - Bitrate used for video recording  
Fragment - Recorded fragment size in second  
RTT - Round trip time per fragment  

NOTE:  
1. Default port is 4433  
2. To know the available command line options, run:```server.exe```  
3. In order to enable the server internal log(by default disabled), run:```set ENABLE_LOG=1```  
before launching the server.   

## Dependency
Server uses HTTPS, for that SSL needs to be used, which require a valid certificate.  
For testing and development a self-signed certificate(```server.pem```) used.  
  
Tested with the following browsers:  
1. Chrome - 85.0.4183.102  
2. Firefox - 80.0.1  
3. Edge - 85.0.564.51  

## Known Issues
Even though the ```echoCancellation``` and ```noiseSuppression``` options are used in  
```getUserMedia```, it does not work for all browsers.  
 
