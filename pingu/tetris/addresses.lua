
while (true) do
  local YSTART = 0;
  local XSTART = 2;
  ypos = YSTART;
  xpos = XSTART;
  color = "#FFFFFF"
    local function wb(loc)
      local byte = memory.readbyte(loc);
      local hex = string.format("%2x", byte);
      gui.text(xpos, ypos, hex, color);
      xpos = xpos + 12;
      if xpos > 250 then
        xpos = XSTART;
        ypos = ypos + 8;
      end;
    end;

  wb(0x0062);
  --  wb(0x006d);

  FCEU.frameadvance();
end;
