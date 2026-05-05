#ifndef CENSOR_H
#define CENSOR_H

int load_dict(const char *filename);
void free_dict(void);
char *censor_text(const char *text, const char *replacement);

#endif