/*  portVideo, a cross platform camera framework
    Copyright (C) 2005-2008 Martin Kaltenbrunner <mkalten@iua.upf.edu>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#if !defined (__x86_64__) && !defined (__ppc64__)
#include "MacVdigCamera.h"

MacVdigCamera::MacVdigCamera(const char* cfg):CameraEngine(cfg)
{
	cameraID = -1;
	
	buffer = NULL;
	
	running=false;
	lost_frames=0;
	
	dstPort = NULL;
	vdImageDesc = NULL;
	pVdg = NULL;
	
	timeout = 5000;
}

MacVdigCamera::~MacVdigCamera()
{
	if (buffer!=NULL) delete []buffer;
}

bool MacVdigCamera::findCamera() {

	OSErr err;
	
	if(!(pVdg = vdgNew()))
	{
		//printf("vdgNew: failed to allocate\n");
		return false;
	}
	
	if(err = vdgInit(pVdg))
	{
		//printf("vdgInit err=%d\n", err);
		printf("no camera found\n");
		return false;
	}

	cameraID = 0;
	return true;
}

bool MacVdigCamera::initCamera() {

	if (cameraID < 0) return false;
	readSettings();

	OSErr err;
	
	if (config.device!=SETTING_AUTO) {
		if(err = vdgRequestSettingsNoGUI(pVdg))
		{
			printf("camera setup cancelled\n");
				//printf("vdgRequestSettings err=%d\n", err);
			return false;
		}
	} else {
		if(err = vdgRequestSettings(pVdg))
		{
			printf("camera setup cancelled\n");
			//printf("vdgRequestSettings err=%d\n", err);
			return false;
		}		
	}

    long nameLength = 256;
	if (err = vdgGetDeviceNameAndFlags(pVdg, cameraName, &nameLength, NULL))
	{
		sprintf(cameraName,"unknown camera");
	}

	long milliSecPerFrame;
	Fixed framerate;
	long bytesPerSecond;
	if (err = vdgGetDataRate(pVdg, &milliSecPerFrame, &framerate, &bytesPerSecond))
	{
		fps = 30;
	} else fps = (int)(framerate/65536);

	//fps = vdgGetFrameRate(pVdg);	
	//printf("%d\n",fps);

	if(err = vdgPreflightGrabbing(pVdg))
	{
		//printf("vdgPreflightGrabbing err=%d\n", err);
		return false;
	}
	
	vdImageDesc = (ImageDescriptionHandle)NewHandle(0);
	if (err = vdgGetImageDescription( pVdg, 
									  vdImageDesc))
	{
		//printf("vdgGetImageDescription err=%d\n", err);
		return false;
	}

	int max_width = (*vdImageDesc)->width;
	int max_height = (*vdImageDesc)->height;
	
	if ((config.width>0) && (config.height>0)) {
		dstPortBounds.left = config.xoff;
		dstPortBounds.right = config.xoff+config.width;
		dstPortBounds.top = config.yoff;
		dstPortBounds.bottom = config.yoff+config.height;
		
		if (dstPortBounds.left<0) dstPortBounds.left = 0;
		if (dstPortBounds.right>max_width) dstPortBounds.right = max_width;
		if (dstPortBounds.top<0) dstPortBounds.top = 0;
		if (dstPortBounds.bottom>max_height) dstPortBounds.bottom = max_height;
	} else {
		dstPortBounds.left = 0;
		dstPortBounds.right = max_width;
		dstPortBounds.top = 0;
		dstPortBounds.bottom = max_height;
	}
	
	if (err = createOffscreenGWorld(	&dstPort,
		//kYUV420CodecType,
		//kComponentVideoSigned,
		//kComponentVideoCodecType,
		k422YpCbCr8CodecType,
		&dstPortBounds))
	{
		printf("createOffscreenGWorld err=%d\n", err);
		return false;	
	}
	
	// Get buffer from GWorld
	pDstData = GetPixBaseAddr(GetGWorldPixMap(dstPort));
	dstDataSize = GetPixRowBytes(GetGWorldPixMap(dstPort)) * (dstPortBounds.bottom - dstPortBounds.top); 
	dstDisplayBounds = dstPortBounds;

	
	// Set the decompression destination to the offscreen GWorld
	if (err = vdgSetDestination(	pVdg, dstPort ))
	{
		//printf("vdgSetDestination err=%d\n", err);
		return false;
	}

	this->width =dstPortBounds.right - dstPortBounds.left;
	this->height = dstPortBounds.bottom - dstPortBounds.top;
	
	buffer = new unsigned char[this->width*this->height*bytes];
	return true;
}

unsigned char* MacVdigCamera::getFrame()
{
	OSErr   err;
	int		isUpdated = 0;

	if (!vdgIsGrabbing(pVdg)) return NULL;
		 
	if (err = vdgIdle( pVdg, &isUpdated))
	{
		//printf("could not grab frame\n");
		return NULL;
	}

	if (isUpdated)
	{
		unsigned char *src = (unsigned char*)pDstData;
		unsigned char *dest = buffer;

		switch (colour) {
			case true: {
				uyvy2rgb(width,height,src,dest);
				break;
			}
			case false: {
				uyvy2gray(width,height,src,dest);
				break;
			}
		}
		lost_frames=0;
		timeout = 1000;
		return buffer;
	} else {
		usleep(1000);
		lost_frames++;
		if (lost_frames>timeout) running=false; // give up after 5 (at init) or 2 (at runtime) seconds
		return NULL;
	}
	
}

bool MacVdigCamera::startCamera()
{

	OSErr err;
	if (err = vdgStartGrabbing(pVdg))
	{
		printf("could not start camera\n");
		return false;
	}

	running = true;
	return true;
}

bool MacVdigCamera::stopCamera()
{
	running=false;

	OSErr err;
	if (err = vdgStopGrabbing(pVdg))
	{
		printf("errors while stopping camera\n");
		return false;
	}

	return true;
}

bool MacVdigCamera::stillRunning() {
	return running;
}

bool MacVdigCamera::resetCamera()
{
  return (stopCamera() && startCamera());
}

bool MacVdigCamera::closeCamera()
{

	if (dstPort)
	{
		disposeOffscreenGWorld(dstPort);
		dstPort = NULL;
	}

	if (vdImageDesc)
	{
		DisposeHandle((Handle)vdImageDesc);
		vdImageDesc = NULL;
	}

	if (pVdg)
	{
		vdgUninit(pVdg);
		vdgDelete(pVdg);
		pVdg = NULL;
	}	

	return true;
}

void MacVdigCamera::showSettingsDialog() {
	vdgStopGrabbing(pVdg);
	vdgShowSettings(pVdg);	
	vdgPreflightGrabbing(pVdg);
	vdgStartGrabbing(pVdg);
}

#endif
