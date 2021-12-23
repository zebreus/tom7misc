#include <string>
#include <stdlib.h>
#include <values.h>

#define yes(n) ( ( ( float( random() ) / float(MAXINT) )< (n) )?1:0)

char * cginame = "latin.cgi";

string handletag(string in) {
       return in;
}

string translate(string in) {
       string out;
       int u;
       for (u=0;u<in.length();u++){
           if (u && in[u] == ' ' && isalpha(in[u-1]) && yes(.4)) {
              out += "us ";
           } else if ((in[u] | 32) == 'u') {
              out += 'V' | (in[u]&32);
           } else out += in[u];
       }
       return out;
}
