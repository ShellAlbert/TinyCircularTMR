#ifndef TMR_HTTP_H__
#define TMR_HTTP_H__


#define VERSION_NO "V0.0.1"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__


#define TMR_VERSION_NO  "TMR httpd server v0.0.1"
#define TMR_RUN_LOG_FILE "/tmp/TMR_httpd.log"
#define TMR_PID_FILE "/tmp/TMR_httpd.pid"

//image and raw file name.
#define TMR_PHASE_A_IMG_FILE "/tmp/TMR_Phase_A.png"
#define TMR_PHASE_B_IMG_FILE "/tmp/TMR_Phase_B.png"
#define TMR_PHASE_C_IMG_FILE "/tmp/TMR_Phase_C.png"
#define TMR_PHASE_ABC_IMG_FILE "/tmp/TMR_Phase_ABC.png"

#define TMR_PHASE_A_RAW_FILE "/tmp/TMR_Phase_A.dat"
#define TMR_PHASE_B_RAW_FILE "/tmp/TMR_Phase_B.dat"
#define TMR_PHASE_C_RAW_FILE "/tmp/TMR_Phase_C.dat"
#define TMR_PHASE_ABC_RAW_FILE "/tmp/TMR_Phase_ABC.dat"


// static device-relevant value.
const char *device_id = "1000000000020033"; // Device ID, 16-bits.
const char *equipment_name = "HuaiRou_TMR_Integration";
const char *manufacture_name = "CEPRI_Sensing_Institute";


#define MAIN_PAGE_HTML \
"<!DOCTYPE html>" \
"<html lang=\"en\">" \
"<head><meta charset=\"UTF-8\"><title>TMR Terminal</title></head>" \
"<body style=\"font-family: sans-serif; text-align: center; padding: 50px;\">" \
"<h1>TMR Current Sensing Integration Terminal</h1>" \
"<style>" \
".device_table {" \
"border-collapse: collapse;" \
"width: 100%;" \
"font-family: Arial, sans-serif; " \
"font-size: 14px;" \
"}" \
".device_table td {" \
"border: 1px solid rgb(6, 6, 6);" \
"padding: 10px 15px;" \
"}" \
".device_table td:first-child {" \
"font-weight: bold;" \
"background-color: #f2f2f2;" \
"width: 40%;" \
"}" \
"</style>" \
"<table class='device_table'>" \
"<tr><td>Measured Range</td> <td>AC 0~1000 Amps</td></tr>" \
"<tr><td>Bandwidth Range</td> <td>DC~10kHz</td></tr>" \
"<tr><td>Supported Channels</td><td>3 Phases, A - B - C</td></tr>" \
"<tr><td>Acquision Interval</td><td>1000 ms</td></tr>" \
"<tr><td>Single Frame Size</td><td>4 Mega Bytes</td></tr>" \
"<tr><td>Raw Data Encoder</td><td>Base64</td></tr>" \
"<tr><td>Built-in History Curve</td><td>Supported PNG format</td></tr>" \
\
"<tr><td>/TMR/Dev_Info</td><td>GET Device basic information</td></tr>" \
"<tr><td>/TMR/Run_Log</td><td>GET running log since started.</td></tr>" \
\
"<tr><td>/TMR/Phase_Curve?Ph=A&Type=Image</td><td>GET Phase A rendering image</td></tr>" \
"<tr><td>/TMR/Phase_Curve?Ph=A&Type=RawData</td><td>GET Phase A Raw Data(base64) in JSON format</td></tr>" \
"<tr><td>/TMR/Phase_Curve?Ph=A&Type=RawData_CSV</td><td>GET Phase A Raw Data in CSV format</td></tr>" \
\
"<tr><td>/TMR/Phase_Curve?Ph=B&Type=Image</td><td>GET Phase B rendering image</td></tr>" \
"<tr><td>/TMR/Phase_Curve?Ph=B&Type=RawData</td><td>GET Phase B Raw Data(base64) in JSON format</td></tr>" \
"<tr><td>/TMR/Phase_Curve?Ph=B&Type=RawData_CSV</td><td>GET Phase B Raw Data in CSV format</td></tr>" \
\
"<tr><td>/TMR/Phase_Curve?Ph=C&Type=Image</td><td>GET Phase C rendering image</td></tr>" \
"<tr><td>/TMR/Phase_Curve?Ph=C&Type=RawData</td><td>GET Phase C Raw Data(base64) in JSON format</td></tr>" \
"<tr><td>/TMR/Phase_Curve?Ph=C&Type=RawData_CSV</td><td>GET Phase C Raw Data in CSV format</td></tr>" \
\
"<tr><td>/TMR/Phase_Curve?Ph=ABC&Type=Image</td><td>GET Phase A&B&C layered rendering image</td></tr>" \
"<tr><td>/TMR/Phase_Curve?Ph=ABC&Type=RawData</td><td>GET Phase A&B&C Mixed Raw Data(base64) in JSON format</td></tr>" \
"<tr><td>/TMR/Phase_Curve?Ph=ABC&Type=RawData_CSV</td><td>GET Phase A&B&C Raw Data in CSV format</td></tr>" \
"</table>" \
"<p>Software Version " TMR_VERSION_NO ", build on " __DATE__ " " __TIME__".</p>" \
"</body></html>"

#define ERROR_PAGE_HTML \
"<!DOCTYPE html>" \
"<html lang=\"en\">" \
"<head><meta charset=\"UTF-8\"><title>TMR Terminal</title></head>" \
"<body><h1>404 Not Found</h1>" \
"<p>The requested resource was not found.</p>"\
"<p>You guy are so funny, DO NOT ATTEMPT TO CRACK ME.</p>"\
"<p>Please obey protocol specification to get what you want, lol.</p>"\
"</body></html>"

#define PAGE_BEGIN_HTML \
"<!DOCTYPE html>" \
"<html lang=\"en\">" \
"<head><meta charset=\"UTF-8\"><title>TMR Terminal</title></head>" \
"<img src=\"data:image/png;base64," 

#define PAGE_END_HTML \
"\" alt=\"Embedded Image\"></body></html>"

//"<img src=\"data:image/png;base64,YOUR_BASE64_STRING_HERE\" alt=\"Embedded Image\">" 
#endif //TMR_HTTP_H__