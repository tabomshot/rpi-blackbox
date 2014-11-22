#ifndef PI_SEND_EVENT_HH
#define PI_SEND_EVENT_HH

const char server_launched_event_url[] = "http://165.194.35.136:8080/BBAServer/RegisterRPi";
const char button_touched_event_url[] = "http://165.194.35.136:8080/BBAServer/NewEvent";

void send_event_launched (void);
void send_event_touched (void);


#endif //PI_SEND_EVENT_HH
