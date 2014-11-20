#include "piMemoryBufferType.hh"


PI_MEMORY_BUFFER::PI_MEMORY_BUFFER (u_int32_t duration)
: frameduration (duration), frame_data_len (0), temp_data_len (0), writer_status (0)
{
    // constructor
    vcos_mutex_create (&buffer_lock, "pi_rtsp_queue");
    vcos_mutex_create (&spspps_lock, "pi_rtsp_spspps");
    
    memset (sps, 0x00, PARAMETER_SET_MAX);
	memset (pps, 0x00, PARAMETER_SET_MAX);
    
    temp_data = (char*) malloc (RTSP_INPUT_BUFFER_MAX);
    frame_data = (char*) malloc (RTSP_INPUT_BUFFER_MAX);
}
    
PI_MEMORY_BUFFER::~PI_MEMORY_BUFFER (void)
{
    // destructor
    vcos_mutex_delete (&buffer_lock);
    vcos_mutex_delete (&spspps_lock);
    
    free (temp_data);
    free (frame_data);
}

void PI_MEMORY_BUFFER::flush_frame_data (void)
{
    if (temp_data_len > 0) {
        vcos_mutex_lock (&buffer_lock);
        memcpy (frame_data, temp_data, temp_data_len);
        frame_data_len = temp_data_len;
        vcos_mutex_unlock (&buffer_lock);
    }
}


// save
void PI_MEMORY_BUFFER::save_sps_data (void* in_data, u_int32_t data_len)
{
    vcos_mutex_lock (&spspps_lock);
    memcpy (sps, in_data, data_len);
    sps_len = data_len;
    vcos_mutex_unlock (&spspps_lock);
}

void PI_MEMORY_BUFFER::save_pps_data (void* in_data, u_int32_t data_len)
{
    vcos_mutex_lock (&spspps_lock);
    memcpy (pps, in_data, data_len);
    pps_len = data_len;
    vcos_mutex_unlock (&spspps_lock);
}

void PI_MEMORY_BUFFER::push_frame_data (void* in_data, u_int32_t data_len)
{
    if (data_len > RTSP_INPUT_BUFFER_MAX) {
        fprintf (stderr, "input data is too big! you can change max size in "
                 "MemoryBufferType.hh\n");
        return;
    }
    
    // parse nal data
    unsigned char* val = (unsigned char*)in_data;
    if (val[0] == 0x00 && val[1] == 0x00 && val[2] == 0x00 && val[3] == 0x01) {
        // nal start code detected,
        switch (val[4]) {
            case 0x27:
                // sps of this encoder
                save_sps_data (val + 4, data_len - 4);
                printf ("%d bytes of sps data detected\n", sps_len);
                break;
            case 0x28:
                // pps of this encoder
                save_pps_data (val + 4, data_len - 4);
                printf ("%d bytes of pps data detected\n", sps_len);
                break;
            default: {
                // flush temp and prepare a new frame
                flush_frame_data ();
                memcpy (temp_data, val + 4, data_len - 4);
                temp_data_len = data_len - 4;
            }
        }// switch
        
    } else {
        // trailing data
        if (temp_data_len + data_len > RTSP_INPUT_BUFFER_MAX) {
            fprintf (stderr, "input data is too big! you can change "
					"max size in MemoryBufferType.hh\n");
            return;
        }
        
        // append to temp_data
        memcpy (temp_data + temp_data_len, val, data_len);
        temp_data_len += data_len;
    }
}

static void* upload_routine (void* arg)
{
    int retval, status;
    pid_t pid;

    printf ("muxing mp4 ...\n");
    if ((pid = fork ()) >= 0) {
        if (pid == 0) {
            // run MP4Box
            // MP4Box -add video.h264 out.mp4
            char* params[] = {"MP4Box", "-add", "video.h264", "out.mp4", NULL};
            execvp ("MP4Box", params);
            
        } else {
            // parent process
            // wait for the child process
            waitpid (pid, &status, 0);
            if ((status & 0xff) == 0) {
                printf ("video \"%s\" saved successfully\n", "out.mp4");
            } else {
                printf ("mp4 muxer terminated unexpectedly\n");
            }
        }
    } else {
        fprintf (stderr, "cannot create mp4 muxer process... "
            "you can install MP4Box by typing sudo apt-get install gpac\n");
        return 0;
    }

    printf ("uploading ...\n");

    return 0;
}
// file writer status
// 0: none
// 1: write_frames 
// 2: convert
void PI_MEMORY_BUFFER::write_frame_on_touched (u_int32_t* flag_touch)
{
    static struct timespec spec;

    if (*flag_touch != 0 && writer_status == WRITER_STATUS_NONE) {
        // touch && no context: new writing context
        printf ("writing file ....\n");

        if ((fp = fopen ("video.h264", "wb")) == NULL) {
            fprintf (stderr, "cannot open \"video.h264\" "
                "for temporary video buffer\n");
            exit (1);
        }

        // write header
        static char header_common[] = {0x00, 0x00, 0x00, 0x01};

        // write sps
        fwrite (header_common, 1, 4, fp);
        fwrite (sps, 1, sps_len, fp);

        // write pps
        fwrite (header_common, 1, 4, fp);
        fwrite (pps, 1, pps_len, fp);

        // record time
        clock_gettime (CLOCK_MONOTONIC, &spec);
        time_now = spec.tv_sec*1000 + spec.tv_nsec/1.0e6;
        time_end = time_now + WRITER_RECORDING_MILLISEC;

        // status transition
        (*flag_touch) = 0;
        writer_status = WRITER_STATUS_WRITING;

    } else if (writer_status != WRITER_STATUS_NONE) {
        if (writer_status == WRITER_STATUS_WRITING) {
            // currently writing frames

            clock_gettime (CLOCK_MONOTONIC, &spec);

            if (spec.tv_sec*1000 + spec.tv_nsec/1.0e6 >= time_end) {
                // time to call the muxer
                pthread_create (&uploader_thread, 0, upload_routine, 0);
                writer_status = WRITER_STATUS_UPLOADING;

            } else {
                // keep writing frames
                fwrite (frame_data, 1, frame_data_len, fp);
            }
        } else {
            // currently uploading and converting
            if (pthread_tryjoin_np(uploader_thread, 0) == 0) {
                // reset status
                writer_status = WRITER_STATUS_NONE;
            }
        }
    }
}


// read
int PI_MEMORY_BUFFER::read_frame_data (void* fTo, u_int32_t* readlen)
{
    int ret = 0;
    
    vcos_mutex_lock (&buffer_lock);
    if (frame_data_len > 0) {
        memcpy (fTo, frame_data, frame_data_len);
        ret = *readlen = frame_data_len;
		frame_data_len = 0;
    }
    vcos_mutex_unlock (&buffer_lock);

    return ret;
}

void PI_MEMORY_BUFFER::read_frame_sps (void* fTo, u_int32_t* readlen)
{
    vcos_mutex_lock (&spspps_lock);
    memcpy (fTo, sps, sps_len);
    *readlen = sps_len;
    vcos_mutex_unlock (&spspps_lock);
}

void PI_MEMORY_BUFFER::read_frame_pps (void* fTo, u_int32_t* readlen)
{
    vcos_mutex_lock (&spspps_lock);
    memcpy (fTo, pps, pps_len);
    *readlen = pps_len;
    vcos_mutex_unlock (&spspps_lock);
}