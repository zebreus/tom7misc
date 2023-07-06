
while (true) do

-- 0x0A - square
-- 0x0E - L shape
-- 0x0B - S
-- 0x02 - T ?
-- 0x07 - J
-- 0x08 - Z
-- 0x12 - bar

  local key = input.get();

--   local yy = 100;
--   for k, v in pairs(key) do
--     gui.text(100, yy, k);
--     yy = yy + 8;
--   end;

  if key["Q"] then
    memory.writebyte(0x0062, 0x02);
  end;
  if key["W"] then
     memory.writebyte(0x0062, 0x07);
  end;
  if key["E"] then
     memory.writebyte(0x0062, 0x08);
  end;
  if key["R"] then
     memory.writebyte(0x0062, 0x0A);
  end;
  if key["T"] then
     memory.writebyte(0x0062, 0x0B);
  end;
  if key["Y"] or key["A"] then
     memory.writebyte(0x0062, 0x0E);
  end;
  if key["U"] or key["S"] then
     memory.writebyte(0x0062, 0x12);
  end;


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
  wb(0x0040);

  wb(0x00a0);
  wb(0x00a1);
  wb(0x00a2);

  FCEU.frameadvance();
end;
