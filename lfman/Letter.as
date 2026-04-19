class Letter extends MovieClip {

  /* startup stuff */
  public function onLoad () {
    trace(666);
    for(var o in this) {
      trace(o);
      trace(o.text);
    }
  }

}
