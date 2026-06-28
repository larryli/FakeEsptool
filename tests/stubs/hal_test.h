/*
 * hal_test.h - Test helper declarations for HAL stubs
 */

#ifndef HAL_TEST_H
#define HAL_TEST_H

void stub_reset(void);
int stub_get_resp_len(void);
const unsigned char *stub_get_resp(void);
int stub_get_baud_changed(void);
unsigned long stub_get_baud_rate(void);
int stub_get_modified(void);

#endif /* HAL_TEST_H */
