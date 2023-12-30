
#include "pdf.h"

static void MakeSimplePDF() {
  PDF::Info info;
  sprintf(info.creator, "pdf_test.cc");
  sprintf(info.producer, "Tom 7");
  sprintf(info.title, "It is a test");
  sprintf(info.author, "None");
  sprintf(info.author, "No subject");
  sprintf(info.date, "30 Dec 2023");

  PDF pdf(PDF::PDF_LETTER_WIDTH,
          PDF::PDF_LETTER_HEIGHT,
          info);

  pdf.Save("test.pdf");
}

int main(int argc, char **argv) {

  MakeSimplePDF();

  printf("OK\n");
  return 0;
}
