/***************************************************************************
 * cm93depth_pi - toolbar / plugin icon
 *
 * A gently sloping seabed (right-angled triangle) with a depth arrow.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "icons.h"

#include <wx/dcmemory.h>
#include <wx/image.h>

wxBitmap MakeLeastDepthIcon(int size) {
  const int s = size;
  wxBitmap bmp(s, s, 24);
  {
    wxMemoryDC dc(bmp);
    const wxColour key(255, 0, 255);  // transparent background
    dc.SetBackground(wxBrush(key));
    dc.Clear();

    // water surface
    dc.SetPen(wxPen(wxColour(30, 110, 200), wxMax(1, s / 16)));
    dc.DrawLine(s / 8, s / 6, s - s / 8, s / 6);

    // seabed: a right-angled triangle, rising gently from deep water on
    // the left to the shallows on the right
    const int bot = s - 1, rise = s * 7 / 16;
    wxPoint bed[] = {wxPoint(0, bot), wxPoint(s - 1, bot - rise), wxPoint(s - 1, bot)};
    dc.SetPen(wxPen(wxColour(120, 90, 40)));
    dc.SetBrush(wxBrush(wxColour(205, 170, 100)));
    dc.DrawPolygon(3, bed);

    // arrow from the surface down to the seabed
    const int ax = s * 9 / 16, top = s / 6;
    const int tip = bot - rise * ax / (s - 1) - 1;  // seabed under the arrow
    const int hw = s / 7;                           // arrow head half-width
    dc.SetPen(wxPen(wxColour(200, 30, 30), wxMax(2, s / 12)));
    dc.DrawLine(ax, top, ax, tip - hw);
    dc.SetBrush(wxBrush(wxColour(200, 30, 30)));
    dc.SetPen(wxPen(wxColour(200, 30, 30)));
    wxPoint head[] = {wxPoint(ax - hw, tip - hw - 1), wxPoint(ax + hw, tip - hw - 1),
                      wxPoint(ax, tip)};
    dc.DrawPolygon(3, head);
    dc.SelectObject(wxNullBitmap);
  }
  wxImage img = bmp.ConvertToImage();
  img.SetMaskColour(255, 0, 255);
  img.InitAlpha();  // mask -> alpha so it blends on any toolbar style
  return wxBitmap(img);
}
