#include <string>
#include <iostream.h>

#define MAXWORDS 15
#define MODULAI 41
#define whitespace(c) ((c==' ')||(c=='\n')||(c=='\r')||(c=='\t'))

string handletag(string in) { return in; }
string mixwords (string * words, int n);

string translate(string in) {
       string words[MAXWORDS];
       string out, thisword;
//       cerr << in<<endl; sleep(1);
       int n = 0;
       for (int u=0;u<in.length();u++) {
           if (whitespace(in[u])) {
              if (thisword != "") {
                 words[n++] = thisword;
                 thisword = "";
              }
           } else if (n && (!isalpha(in[u]) || n > MAXWORDS)) {
              // mix them up
              words[n] = thisword;
              out += mixwords(words,n);
              if (!isalpha(in[u])) out += (char) in[u];
              n=0;
              thisword="";
           } else thisword += in[u];
       }
       if (thisword != "") words[n++] = thisword;
       out += mixwords (words,n);
return out;
}

string mixwords(string * words, int n) {
              string out;
              int left=n, now=0;
              while (left) {
                 now = (now + MODULAI) % n;
                 while (words[now] == "") {
                       now++;
                       now %= n;
                 }
                 out += ' ' +words[now];
                 //words[now] = "";
                 left--;
              }
return out;
}
