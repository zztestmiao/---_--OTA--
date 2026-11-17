#ifndef OTA_REQUEST_H
#define OTA_REQUEST_H
/* 主循环独占调用；提交READY与软复位分两次手动请求，失败不自动重试。 */
void ota_request_poll(void);
#endif
