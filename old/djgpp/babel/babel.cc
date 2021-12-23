#include <iostream.h>
#include <string>
#include <stdio.h>
#include <file.h>

string readfile(FILE*);

string translate(string);
string handletag(string);
string dotags(string);

#define ROOTPATH "/usr/home/snoot/public_html/"

#define CGIPATH ((string)"/cgi-bin/"+(string)cginame)

extern char * cginame;

int main () {
    string input, out;
    string tag, text;
    int intag=0;
    input = readfile(stdin);

    for (int n=0;n<input.length();n++) {
        if (input[n] == '<') {
           out += translate(text);
           tag = "";
           tag += '<';
           intag = 1;
        } else if (input[n] == '>') {
           text = "";
           tag += '>';
           out += dotags(tag);
           intag = 0;
        } else if (intag) tag += input[n];
          else           text += input[n];
    }
    out += translate(text);
    cout << out;
}

string readfile(FILE*f) {
    string out;
    int c;
    while (EOF != (c=getc(f)))
          out += (char)c;
    return out;
}

string dotags(string in) {
       if (in.length() > 1 && in[1]|32 == 'a') {
          // anchor tag
          string findy = "href=";
          int at = in.find_first_of(findy,0);
          if (!at) return in; // return (string)"(didn't find+ "+in;
          at += 5;
          if (in[at] == '\"') at++;
          string out;
          char oldy = in[at];
          in[at]= '\0';
          out = in.c_str();
          out += CGIPATH + '?' + oldy;
          out += (string)(in.c_str() + at + 1);
          return out;
       } else return handletag(in);
}
