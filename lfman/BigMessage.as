
class BigMessage extends MovieClip {
  var message;

  var timeleft = 0;

  public function onEnterFrame() {
    if (timeleft != undefined) {
      if (timeleft == 0) {
	message.text = '';
      } else {
	timeleft --;
      }
    }
  }

  public function complain(s) {
    message.htmlText = '<font color="#FF0000">' + s + '</font>';
    timeleft = undefined;
  }

  public function say(s) {
    message.text = s;
    timeleft = 12 * 8;
  }

}
