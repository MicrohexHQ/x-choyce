#ifndef X_XFT_CPP
#define X_XFT_CPP

#include "x_xft.hpp"

using namespace x;
namespace t = x::type;

xft::xft(Display * dpy,
         XVisualInfo * const visual_info, const Colormap & colormap,
         unsigned int width, unsigned int height)
  : m_dpy(dpy),
    m_visual_info(visual_info), m_colormap(colormap),
    m_width(width), m_height(height),
{
  m_drawable = XCreatePixmap(
      m_dpy, DefaultRootWindow(m_dpy), width, height, m_visual_info->depth);
  m_xftdraw = XftDrawCreate(m_dpy, m_drawable, m_visual_info->visual, m_colormap);
}

xft::~xft(void)
{
  XftDrawDestroy(m_xftdraw);

  for (auto & item : m_colors) {
    XftColorFree(m_dpy, m_visual_info->visual, m_colormap, &item.second);
  }

  for (auto & item : m_fonts) {
    XftFontClose(m_dpy, item.second);
  }

  if (m_drawable != None) XFreePixmap(m_dpy, m_drawable);
  if (m_colormap != None) XFreeColormap(m_dpy, m_colormap);
}

xft &
xft::draw_string_utf8(const std::string & text,
                      const unsigned int & x, const unsigned int & y)
{
  XftDrawStringUtf8(m_xftdraw, &m_fg_color, m_font, x, y,
                    (FcChar8 *)text.c_str(), text.length());
  return *this;
}

XGlyphInfo
xft::text_extents_utf8(const t::fontname & fontname, const std::string & text)
{
  XGlyphInfo extents;
  XftTextExtentsUtf8(m_dpy, (*this)[fontname],
                     (FcChar8 *)text.c_str(), text.length(), &extents);
  return extents;
}

XftColor * const
xft::operator[](const x::type::colorname & colorname)
{
  try {
    return &m_colors.at(*colorname);
  } catch (...) {
    XftColorAllocName(m_dpy, m_visual_info->visual, m_colormap,
                      colorname->c_str(), &m_colors[*colorname]);
    return &m_colors[*colorname];
  }
}

XftFont * const
xft::operator[](const t::fontname & fontname)
{
  try {
    return m_fonts.at(*fontname);
  } catch (...) {
    m_fonts[*fontname] =
      XftFontOpenName(m_dpy, DefaultScreen(m_dpy), fontname->c_str());
    return m_fonts[*fontname];
  }
}

#endif // X_XFT_CPP
