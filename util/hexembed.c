#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  if (argc < 3) {
    printf("Usage:\n\thexembed <filename> <variablename>\n");
    return 1;
  }

  const char *fname = argv[1];
  FILE *fp = fopen(fname, "rb");
  if (!fp) {
    fprintf(stderr, "Error opening file: %s.\n", fname);
    return 1;
  }

  fseek(fp, 0, SEEK_END);
  const int fsize = ftell(fp);

  fseek(fp, 0, SEEK_SET);
  unsigned char *b = malloc(fsize);

  fread(b, fsize, 1, fp);
  fclose(fp);

  printf("/* Embedded file: %s */\n", fname);
  printf("const int %s_size = %d;\n", argv[2], fsize);
  printf("const unsigned char %s[] = {\n", argv[2]);

  int i;
  for (i = 0; i < fsize; ++i) {
    printf("0x%02x%s", b[i],
           i == fsize - 1 ? "" : ((i + 1) % 16 == 0 ? ",\n" : ","));
  }
  printf("\n};\n");

  free(b);
  return 0;
}
