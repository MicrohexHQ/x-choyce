#include "x_client.hpp"

#include <cstring> // memset
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_image.h>

x_client::x_client(x_connection & c, const xcb_window_t & window)
  : _c(c), _window(window)
{
  _c.register_handler(XCB_CONFIGURE_NOTIFY, this);
  _c.register_handler(XCB_PROPERTY_NOTIFY, this);
  _c.update_input(_window, XCB_EVENT_MASK_STRUCTURE_NOTIFY
                           | XCB_EVENT_MASK_PROPERTY_CHANGE);
  update_geometry();
  update_net_wm_desktop();
  update_parent_window();
  update_name_window_pixmap();

  update_net_wm_icon();
}

x_client::~x_client(void)
{
  _c.deregister_handler(XCB_CONFIGURE_NOTIFY, this);
  _c.deregister_handler(XCB_PROPERTY_NOTIFY, this);

  xcb_free_pixmap(_c(), _net_wm_icon_pixmap);
}

      rectangle &  x_client::rect(void)       { return _rectangle; }
const rectangle &  x_client::rect(void) const { return _rectangle; }
      xcb_window_t & x_client::window(void)          { return _window; }
const xcb_window_t & x_client::window(void) const    { return _window; }
      xcb_window_t & x_client::parent(void)          { return _parent; }
const xcb_window_t & x_client::parent(void) const    { return _parent; }

xcb_window_t &
x_client::name_window_pixmap(void)
{
  return _name_window_pixmap;
}

const xcb_window_t &
x_client::name_window_pixmap(void) const
{
  return _name_window_pixmap;
}

unsigned int x_client::net_wm_desktop(void) const { return _net_wm_desktop; }

void x_client::update_geometry(void)
{
  xcb_get_geometry_reply_t * geometry_reply =
    xcb_get_geometry_reply(_c(), xcb_get_geometry(_c(), _window), NULL);

  _rectangle.x()      = geometry_reply->x;
  _rectangle.y()      = geometry_reply->y;
  _rectangle.width()  = geometry_reply->width;
  _rectangle.height() = geometry_reply->height;

  delete geometry_reply;
}

bool
x_client::handle(xcb_generic_event_t * ge)
{
  if (XCB_CONFIGURE_NOTIFY == (ge->response_type & ~0x80)) {
    xcb_configure_notify_event_t * e = (xcb_configure_notify_event_t *)ge;
    if (e->window == _window) {
      _rectangle.x() = e->x; _rectangle.y() = e->y;
      _rectangle.width() = e->width; _rectangle.height() = e->height;
    }

    if (e->window == _c.root_window() || e->window == _window) {
      update_parent_window();
      update_name_window_pixmap();
    }

    return true;

  } else if (XCB_PROPERTY_NOTIFY == (ge->response_type & ~0x80)) {
    xcb_property_notify_event_t * e = (xcb_property_notify_event_t *)ge;

    if (e->window == _window && e->atom == a_net_wm_desktop) {
      update_net_wm_desktop();
    }

    return true;
  }

  return false;
}

// private

void
x_client::update_net_wm_desktop(void)
{
  xcb_generic_error_t * error = NULL;
  xcb_get_property_cookie_t c = xcb_get_property(
      _c(), false, _window, a_net_wm_desktop, XCB_ATOM_CARDINAL, 0, 32);
  xcb_get_property_reply_t * r = xcb_get_property_reply(_c(), c, &error);

  if (error || r->value_len == 0) {
    delete error;
    _net_wm_desktop = 0;
  } else {
    _net_wm_desktop = *(unsigned int *)xcb_get_property_value(r);
  }

  delete r;
}

void
x_client::update_parent_window(void)
{
  xcb_window_t next_parent = _window;

  while (next_parent != _c.root_window() && next_parent != XCB_NONE) {
    _parent = next_parent;
    next_parent = std::get<0>(_c.query_tree(next_parent));
  }
}

void
x_client::update_name_window_pixmap(void)
{
  xcb_free_pixmap(_c(), _name_window_pixmap);
  _name_window_pixmap = xcb_generate_id(_c());
  xcb_void_cookie_t c = xcb_composite_name_window_pixmap_checked(
      _c(), _parent, _name_window_pixmap);

  xcb_generic_error_t * error = xcb_request_check(_c(), c);

  if (error) {
    delete error;
    _name_window_pixmap = XCB_NONE;
  }
}

void
x_client::update_net_wm_icon(void)
{
  xcb_free_pixmap(_c(), _net_wm_icon_pixmap);
  _net_wm_icon_pixmap = XCB_NONE;

  xcb_generic_error_t * error;
  xcb_get_property_cookie_t c = xcb_ewmh_get_wm_icon(_c.ewmh(), _window);

  xcb_ewmh_get_wm_icon_reply_t wm_icon;
  std::memset(&wm_icon, 0, sizeof(xcb_ewmh_get_wm_icon_reply_t));
  xcb_ewmh_get_wm_icon_reply(_c.ewmh(), c, &wm_icon, &error);

  if (error) {
    delete error;
    return;

  } else if (0 < xcb_ewmh_get_wm_icon_length(&wm_icon)) {

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t * data = NULL;

    xcb_ewmh_wm_icon_iterator_t iter = xcb_ewmh_get_wm_icon_iterator(&wm_icon);
    for (; iter.rem; xcb_ewmh_get_wm_icon_next(&iter)) {
      if (iter.width > width) {
        width = iter.width;
        height = iter.height;
        data = iter.data;
      }
    }

    _icon_geometry.first = width;
    _icon_geometry.second = height;

    _net_wm_icon_pixmap = xcb_generate_id(_c());
    xcb_create_pixmap(
        _c(), 32, _net_wm_icon_pixmap, _c.root_window(), width, height);

    xcb_image_t * image = xcb_image_create_native(
        _c(), width, height, XCB_IMAGE_FORMAT_Z_PIXMAP, 32, NULL, 0, NULL);

    image->data = (uint8_t *)data;

    alpha_transform(image->data, width, height);

    xcb_gcontext_t gc = xcb_generate_id(_c());
    xcb_create_gc(_c(), gc, _net_wm_icon_pixmap, 0, NULL);

    xcb_image_put(_c(), _net_wm_icon_pixmap, gc, image, 0, 0, 0);

    xcb_image_destroy(image);
    xcb_free_gc(_c(), gc);
  }

  xcb_ewmh_get_wm_icon_reply_wipe(&wm_icon);
}

// stolen from openbox: tests/icons.c
void
x_client::alpha_transform(uint8_t * data, unsigned int w, unsigned int h)
{
  for (uint32_t j = 0; j < w * h; j++) {
    unsigned char a = (unsigned char)data[j*sizeof(uint32_t)+3];
    unsigned char r = (unsigned char)data[j*sizeof(uint32_t)+0];
    unsigned char g = (unsigned char)data[j*sizeof(uint32_t)+1];
    unsigned char b = (unsigned char)data[j*sizeof(uint32_t)+2];

    // background color
    unsigned char bgr = 0;
    unsigned char bgg = 0;
    unsigned char bgb = 0;

    r = bgr + (r - bgr) * a / 256;
    g = bgg + (g - bgg) * a / 256;
    b = bgb + (b - bgb) * a / 256;

    data[j*sizeof(uint32_t)+0] = (char)r;
    data[j*sizeof(uint32_t)+1] = (char)g;
    data[j*sizeof(uint32_t)+2] = (char)b;
  }
}

// free functions

std::list<x_client>
make_x_clients(x_connection & c, const std::vector<xcb_window_t> & windows)
{
  std::list<x_client> x_clients;
  for (auto & window : windows) { x_clients.emplace_back(c, window); }
  return x_clients;
}

bool operator==(const x_client & x_client, const xcb_window_t & window)
{
  return x_client._window == window;
}

bool operator==(const xcb_window_t & window, const x_client & x_client)
{
  return x_client == window;
}
