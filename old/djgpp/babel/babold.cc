
//  babel.cc for Snoot.com
//
//  Acts as a filter, converting html stdin to html stdout.
//

#include <iostream.h>
#include <string>

string translate(string text);  // this is defined differently for each
                                // snooting language.
string handletag(string text);  // handles tags in each snooting language
                                // by default just return it.

int main () {
    string token;
    string input;
    string output;
    int intag=0, tryagain=0;

    while (tryagain || cin >> token) {

          for (int n=0;n<token.length();n++) {
              if (token[n] == '<') {
                 token[n] = '\0';
                 output += translate(input); input = "";
                 output += translate((string)(token.c_str()));
                 token[n] = '<';
                 token = (string)(n+token.c_str());
              break;
              }
          }
    tryagain = 0;
          if (intag || token[0] == '<') { //got tag
             if (!intag) { output += translate(input); input = ""; }
             for (int n=0;n<token.length();n++) {
               if (token[n] == '>') {
                  intag = 0;
                  token[n] = '\0';
                  input += (string)(token.c_str()) + '>';
//                cerr << "tag to handle: " << input << endl;
                  output += handletag(input);
                  input = "";
                  token = (string)(token.c_str()+(n+1));
                  if (token != "") tryagain = 1;
                  goto foundit;
               }
             }
             intag = 1;
             input += token + ' ';
foundit:;
          } else input += token + ' ';

    }
    output += handletag(input);

    cout << output;
}

