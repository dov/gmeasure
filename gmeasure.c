//======================================================================
//  gmeasure.c - An application for measuring distances.
//
//  Dov Grobgeld <dov.grobgeld@gmail.com>
//  Sat Oct 30 20:15:39 2010
//----------------------------------------------------------------------
#include <stdlib.h>
#include <gtk/gtk.h>
#include <math.h>
#include "dovtk-lasso.h"
#include "gtk-image-viewer.h"
#include "giv-calibrate-dialog.h"
#ifdef USE_HILDON
#include <hildon/hildon.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

#define _(x) (x)
#define N_(x) (x)
#define VERSION "0.1.0"

DovtkLasso *lasso = NULL;
GtkWidget *w_imgv = NULL;
GtkWidget *w_calibrate_dialog = NULL;
double start_x, start_y, end_x, end_y;
gboolean do_measure = FALSE;
//gboolean do_calibrate = FALSE;
double last_move_x = -1;
double last_move_y = -1;
double target_size = 1.0;
double pixel_size = 1.0;
double last_dist = -1;
gchar *pixel_size_unit = NULL;
double picking_start_x=-1, picking_start_y=-1;
int picking = -1;

#if USE_HILDON
void grab_zoom_keys(GtkWidget *w_top)
{
    Display *dpy = gdk_x11_drawable_get_xdisplay(w_top->window);
    Window win = GDK_WINDOW_XID(w_top->window);

    Atom atom = XInternAtom( dpy, "_HILDON_ZOOM_KEY_ATOM", 0);
    unsigned long val = 1;
    XChangeProperty (dpy, win,
                     XInternAtom( dpy, "_HILDON_ZOOM_KEY_ATOM", 0),
                     XA_INTEGER, 32,
                     PropModeReplace, (unsigned char *) &val,
                     1);
}
#endif

static void
draw_caliper(cairo_t *cr,
             DovtkLassoContext lasso_context,
             double x0, double y0,
             double x1, double y1,
             const char *caliper_text)
{
    int margin = 0;
    if (lasso_context != DOVTK_LASSO_CONTEXT_PAINT)
        margin = 5;

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    double angle = atan2(y1-y0,x1-x0);
    cairo_translate(cr,
                    0.5 * (x0+x1),
                    0.5 * (y0+y1));
    cairo_rotate(cr, angle);
    double dist = sqrt((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0));

    if (lasso_context == DOVTK_LASSO_CONTEXT_PAINT)
        cairo_set_source_rgba(cr, 10*0x4d/255.0,1.0*0xaa/255.0,0,0.5);
    else if (lasso_context == DOVTK_LASSO_CONTEXT_LABEL)
        dovtk_lasso_set_color_label(lasso, cr, 1);
    
    cairo_rectangle(cr, -dist/2-margin, -20-margin,
                    dist+2*margin, 20+2*margin);
    cairo_fill(cr);

    if (lasso_context == DOVTK_LASSO_CONTEXT_PAINT)
        cairo_set_source_rgb(cr, 0x50/255.0,0x2d/255.0,0x16/255.0);
    else if (lasso_context == DOVTK_LASSO_CONTEXT_LABEL)
        dovtk_lasso_set_color_label(lasso, cr, 2);

    double calip_height = 50;
    cairo_move_to(cr, -dist/2+margin,calip_height/2+margin); 
    double dy = -(calip_height+3*margin)/3;
    cairo_rel_curve_to(cr,
                       -15-2*margin,dy,
                       -15-2*margin,2.5*dy,
                       -15-2*margin,3*dy);
    cairo_line_to(cr, -dist/2+margin,-calip_height/2-margin); 
                  
    cairo_close_path(cr);
        
    if (lasso_context == DOVTK_LASSO_CONTEXT_LABEL) {
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 5);
        cairo_stroke(cr);
        dovtk_lasso_set_color_label(lasso, cr, 3);
    }

    cairo_move_to(cr, dist/2-margin,calip_height/2+margin); 
    cairo_rel_curve_to(cr,
                       15+2*margin,dy,
                       15+2*margin,2.5*dy,
                       15+2*margin,3*dy);
    cairo_line_to(cr, dist/2-margin,-calip_height/2-margin); 
                  
    cairo_close_path(cr);

    if (lasso_context == DOVTK_LASSO_CONTEXT_PAINT) 
        cairo_fill(cr);
    else {
        cairo_set_line_width(cr, 5);
        cairo_fill_preserve(cr);
        cairo_stroke(cr);
    }
    
    // No need to draw text for label context
    if (lasso_context == DOVTK_LASSO_CONTEXT_LABEL)
        return;

    // Draw the distance in the middle
    PangoFontDescription *font_descr = pango_font_description_from_string("Sans 15");

    PangoContext *context = pango_cairo_create_context(cr);
    cairo_font_options_t *options =  cairo_font_options_create();
    cairo_font_options_set_hint_style(options,CAIRO_HINT_STYLE_NONE);
    pango_cairo_context_set_font_options(context, options);
    PangoLayout *pango_layout = pango_layout_new(context);

    pango_layout_set_font_description(pango_layout, font_descr);
    pango_layout_set_text(pango_layout, caliper_text, -1);
    int layout_width, layout_height;
    pango_layout_get_size(pango_layout, &layout_width, &layout_height);

    cairo_move_to(cr, -0.5*layout_width/PANGO_SCALE,-0.9*layout_height/PANGO_SCALE);
    if (lasso_context==DOVTK_LASSO_CONTEXT_PAINT)
        cairo_set_source_rgba(cr, 0,0,0,1); // 0x50/255.0,0x2d/255.0,0x16/255.0,1);

    pango_cairo_show_layout(cr, pango_layout);
    g_object_unref(pango_layout);
    pango_font_description_free(font_descr);
    g_object_unref(context);
    cairo_font_options_destroy(options);
}

/** 
 * Draw  whatever overlay you want on the image. If the do_mask
 * is on, then you should paint in black and with a pen that
 * is thicker than the drawing. 
 */
void my_lasso_draw(cairo_t *cr,
                   DovtkLassoContext context,
                   gpointer user_data)
{
    // Draw a rectangle
    //    cairo_rectangle(cr, min_x, min_y, abs(end_x-start_x), abs(end_y-start_y));
    if (start_x>0) {
        double sx,sy,ex,ey;
        gtk_image_viewer_img_coord_to_canv_coord(GTK_IMAGE_VIEWER(w_imgv),
                                                 start_x,
                                                 start_y,
                                                 &sx, &sy);
        gtk_image_viewer_img_coord_to_canv_coord(GTK_IMAGE_VIEWER(w_imgv),
                                                 end_x,
                                                 end_y,
                                                 &ex,&ey);

        gchar *caliper_text = g_strdup_printf("%.1f%s",
                                              last_dist*pixel_size,
                                              pixel_size_unit);
        draw_caliper(cr,
                     context,
                     sx,sy,
                     ex,ey,
                     caliper_text);
        g_free(caliper_text);
    }

    cairo_stroke(cr);
}

int cb_button_press(GtkWidget      *widget,
                    GdkEventButton *event,
                    gpointer        user_data)
{
    int ret = 0;
    if (!(event->state & GDK_CONTROL_MASK)
        && event->button == 1) {

        int cx = (int)event->x;
        int cy = (int)event->y;
        double x,y;
    
        gtk_image_viewer_canv_coord_to_img_coord(GTK_IMAGE_VIEWER(w_imgv),
                                                 cx, cy, &x, &y);
        last_move_x = x;
        last_move_y = y;

        picking = dovtk_lasso_get_label_for_pixel(lasso, cx, cy);
        if (picking) {
            picking_start_x = last_move_x;
            picking_start_y = last_move_y;
            ret = 1;
        }
        else if (do_measure) {
            start_x = last_move_x;
            start_y = last_move_y;
            end_x = start_x;
            end_y = start_y;
            dovtk_lasso_update(lasso);
            ret = 1;
        }
    }

    return ret;
}

int cb_button_release(GtkWidget      *widget,
                      GdkEventButton *event,
                      gpointer        user_data)
{
    do_measure = FALSE;
    picking = -1;

    return FALSE;
}

int cb_motion_notify(GtkWidget      *widget,
                     GdkEventMotion *event,
                     gpointer        user_data)
{
    int ret = 0;
    int cx = (int)event->x;
    int cy = (int)event->y;
    double x,y;

    gtk_image_viewer_canv_coord_to_img_coord(GTK_IMAGE_VIEWER(w_imgv),
                                             cx, cy, &x, &y);

    if (picking>0) {
        double dx = x - picking_start_x;
        double dy = y - picking_start_y;
        if (picking == 1 || picking==2) {
            start_x += dx;
            start_y += dy;
        }
        if (picking == 1 || picking==3) {
            end_x += dx;
            end_y += dy;
        }
        picking_start_x = x;
        picking_start_y = y;
        ret = 1;
    }
    else if (do_measure) {
        //    printf("button motion\n");
        end_x = x;
        end_y = y;
        ret = 1;
    }

    last_dist = sqrt(pow(end_x-start_x,2)
                     + pow(end_y-start_y,2));
    if (w_calibrate_dialog)
        giv_calibrate_dialog_set_last_measure_distance_in_pixels(
          GIV_CALIBRATE_DIALOG(w_calibrate_dialog),
          last_dist);

    if (lasso)
        dovtk_lasso_update(lasso);

    last_move_x = x;
    last_move_y = y;

    if (ret)
        gdk_event_request_motions(event);

    return ret;
}

void cb_measure_button (GtkWidget *w_check_button_measure,
                               gpointer  user_data)
{
    do_measure = TRUE;
}

gint
cb_key_press_event (GtkWidget   *widget,
                    GdkEventKey *event)
{
    gint k;

    k = event->keyval;
    switch (k) {
#ifdef USE_HILDON
    case GDK_F7:
        gtk_image_viewer_zoom_in(w_imgv, -1, -1, 1.5);
        break;
    case GDK_F8:
        gtk_image_viewer_zoom_out(w_imgv, 0, 0, 1.5);
        break;
#else
    case 'm':
        do_measure = TRUE;
        gtk_widget_queue_draw(w_imgv);
        break;
    case 'c':
        gtk_widget_show(w_calibrate_dialog);
        break;
#endif
    default:
        ;
    }
    return FALSE;
}

int cb_about()
{
    GtkWidget *about_window;
    GtkWidget *vbox;
    GtkWidget *label;
  
    about_window = gtk_dialog_new ();
    gtk_dialog_add_button (GTK_DIALOG (about_window),
                           GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response (GTK_DIALOG (about_window),
                                     GTK_RESPONSE_OK);
  
    gtk_window_set_title(GTK_WINDOW (about_window), "About GMeasure");
  
    gtk_window_set_resizable (GTK_WINDOW (about_window), FALSE);

    g_signal_connect (G_OBJECT (about_window), "delete-event",
                      G_CALLBACK (gtk_widget_destroy), NULL);
  
    vbox = gtk_vbox_new (FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (about_window)->vbox), vbox, FALSE, FALSE, 0);

#if 0
    GError *err = NULL;
    GdkPixbuf *icon;
    icon = gdk_pixbuf_new_from_inline(sizeof(image_logo_150_inline),
                                      image_logo_150_inline,
                                      FALSE,
                                      &err);
    w_image = gtk_image_new_from_pixbuf(icon);

    gtk_box_pack_start (GTK_BOX (vbox), w_image, FALSE, FALSE, 0);
#endif
  
    label = gtk_label_new (NULL);
    gtk_misc_set_alignment (GTK_MISC (label), 0.5, 0.5);
    gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
    gchar *markup = g_strdup_printf
        ("<span size=\"xx-large\" weight=\"bold\">GMeasure "VERSION"</span>\n\n"
         "%s\n\n"
         "<span>%s\n%sEmail: <tt>&lt;%s&gt;</tt></span>\n",
         _("A measuring tool"),
         _("A GPL program 2010\n"),
         _("Dov Grobgeld\n"),
         _("dov.grobgeld@gmail.com"));
    gtk_label_set_markup (GTK_LABEL (label), markup);
    g_free (markup);
    gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);
  
    gtk_widget_show_all (about_window);
    gtk_dialog_run (GTK_DIALOG (about_window));
    gtk_widget_destroy (about_window);
    return TRUE;
}

static GtkWidget *entry_new_from_double(double init_val)
{
    GtkWidget *w_entry = gtk_entry_new();
    gchar *text = g_strdup_printf("%.3g", init_val);
    gtk_entry_set_text(GTK_ENTRY(w_entry),
                       text);
    g_free(text);

    return w_entry;
}

static GtkWidget *entry_new_from_string(const char *text)
{
    GtkWidget *w_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w_entry), text);

    return w_entry;
}

static GtkWidget *label_left_new(const gchar *label)
{
    gchar *markup = g_strdup_printf("%s:", label);
    GtkWidget *w_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(w_label), markup);
    g_free(markup);
    gtk_misc_set_alignment(GTK_MISC(w_label), 0, 0.5);
    return w_label;
}

static int cb_menu_tbd()
{
    GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW(NULL),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_ERROR,
                                     GTK_BUTTONS_CLOSE,
                                     "Sorry. Not implemented yet");
    gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
  
    return;
}

int cb_menu_set_pixelsize()
{
    GtkWidget *w_dialog;
    GtkWidget *w_table;
  
    w_dialog = gtk_dialog_new ();
    gtk_dialog_add_button (GTK_DIALOG (w_dialog),
                           GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_add_button (GTK_DIALOG (w_dialog),
                           GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_set_default_response (GTK_DIALOG (w_dialog),
                                     GTK_RESPONSE_OK);
  
    gtk_window_set_title(GTK_WINDOW (w_dialog), "Set pixel size");
  
    gtk_window_set_resizable (GTK_WINDOW (w_dialog), FALSE);

    g_signal_connect (G_OBJECT (w_dialog), "delete-event",
                      G_CALLBACK (gtk_widget_destroy), NULL);
  
    w_table = gtk_table_new(2,2, FALSE);
    int row = 0;
    gtk_table_attach(GTK_TABLE(w_table),
                     label_left_new("Pixel size:"),
                     0, 1,
                     row, row+1,
                     (GtkAttachOptions)(GTK_FILL|GTK_EXPAND),
                     (GtkAttachOptions)(0),
                     0,0);
    GtkWidget *w_entry_pixelsize = entry_new_from_double(pixel_size);
    gtk_table_attach(GTK_TABLE(w_table),
                     w_entry_pixelsize,
                     1,2,
                     row, row+1,
                     (GtkAttachOptions)(GTK_FILL|GTK_EXPAND),
                     (GtkAttachOptions)(0),
                     0,0);
    row++;
    gtk_table_attach(GTK_TABLE(w_table),
                     label_left_new("Pixel size unit:"),
                     0, 1,
                     row, row+1,
                     (GtkAttachOptions)(GTK_FILL|GTK_EXPAND),
                     (GtkAttachOptions)(0),
                     0,0);
    GtkWidget *w_entry_pixelsize_unit = entry_new_from_string(pixel_size_unit);
    gtk_table_attach(GTK_TABLE(w_table),
                     w_entry_pixelsize_unit,
                     1,2,
                     row, row+1,
                     (GtkAttachOptions)(GTK_FILL|GTK_EXPAND),
                     (GtkAttachOptions)(0),
                     0,0);
                     
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (w_dialog)->vbox),
                        w_table, FALSE, FALSE, 0);

    gtk_widget_show_all (w_dialog);
    gtk_dialog_run (GTK_DIALOG (w_dialog));
    g_free(pixel_size_unit);
    pixel_size_unit = g_strdup(gtk_entry_get_text(GTK_ENTRY(w_entry_pixelsize_unit)));
    pixel_size =atof(gtk_entry_get_text(GTK_ENTRY(w_entry_pixelsize)));
    gtk_widget_destroy (w_dialog);

    return TRUE;
}

int cb_menu_calibrate()
{
    gtk_widget_show_all(w_calibrate_dialog);
    return TRUE;
}

static void
cb_calib_changed(GtkWidget *calib_dialog,
                 double _pixel_size,
                 const char *unit,
                 gpointer user_data)
{
    pixel_size = _pixel_size;
    g_free(pixel_size_unit);
    pixel_size_unit = g_strdup(unit);
    if (lasso)
        dovtk_lasso_update(lasso);
}

static void
cb_calib_hide(GtkWidget *calib_dialog,
              gpointer user_data)
{
    gtk_widget_hide(calib_dialog);
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);
#if USE_HILDON
    GtkWidget *w_top = hildon_window_new();
    gtk_widget_show(w_top);
    grab_zoom_keys(w_top);

    // Can't get pannable area to work with GtkImageViewer
    // GtkWidget *w_sw = hildon_pannable_area_new();
    GtkWidget *w_sw = gtk_scrolled_window_new(NULL,NULL);
    GtkWidget *w_app_menu = hildon_app_menu_new();
    GtkWidget *w_button_measure = gtk_button_new_with_label("Measure");
    g_signal_connect(w_button_measure, "clicked",
                     G_CALLBACK(cb_measure_button), NULL);
    hildon_app_menu_append(HILDON_APP_MENU(w_app_menu),
                           GTK_BUTTON(w_button_measure));

    GtkWidget *w_button_calibrate = gtk_button_new_with_label("Calibrate");
    g_signal_connect(w_button_calibrate, "clicked",
                     G_CALLBACK(cb_menu_calibrate), NULL);
    hildon_app_menu_append(HILDON_APP_MENU(w_app_menu),
                           GTK_BUTTON(w_button_calibrate));

    GtkWidget *w_button_snapshot = gtk_button_new_with_label("Snapshot Image");
    g_signal_connect(w_button_snapshot, "clicked",
                     G_CALLBACK(cb_menu_tbd), NULL);
    hildon_app_menu_append(HILDON_APP_MENU(w_app_menu),
                           GTK_BUTTON(w_button_snapshot));

    GtkWidget *w_button_about = gtk_button_new_with_label("About");
    g_signal_connect(w_button_about, "clicked",
                     G_CALLBACK(cb_about), NULL);
    hildon_app_menu_append(HILDON_APP_MENU(w_app_menu),
                           GTK_BUTTON(w_button_about));

    hildon_window_set_app_menu(HILDON_WINDOW(w_top),
                               HILDON_APP_MENU(w_app_menu));
    gtk_widget_show_all(w_app_menu);
#else
    GtkWidget *w_top = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *w_sw = gtk_scrolled_window_new(NULL,NULL);
#endif
    pixel_size_unit = g_strdup("");

    g_signal_connect(G_OBJECT(w_top), "delete-event",
                     G_CALLBACK(gtk_main_quit), NULL);

    char *filename = g_strdup("/home/dov/pictures/maja.png");
    if (argc > 1)
        filename = argv[1];
    w_imgv = gtk_image_viewer_new_from_file(filename);
    gtk_container_add(GTK_CONTAINER(w_top),
                      w_sw);
    gtk_container_add(GTK_CONTAINER(w_sw),
                      w_imgv);
    gtk_widget_set_size_request(w_imgv, 500,500);
                     
    // Create the lasso and set up events for lasso
    lasso = dovtk_lasso_create(w_imgv,
                               &my_lasso_draw,
                               NULL);

    gtk_widget_add_events(w_imgv,
                          GDK_BUTTON_MOTION_MASK
                          | GDK_BUTTON_PRESS_MASK
                          | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(G_OBJECT(w_imgv), "button-press-event",
                     G_CALLBACK(cb_button_press), NULL);
    g_signal_connect(G_OBJECT(w_imgv), "button-release-event",
                     G_CALLBACK(cb_button_release), NULL);
    g_signal_connect(G_OBJECT(w_imgv), "motion-notify-event",
                     G_CALLBACK(cb_motion_notify), NULL);
    g_signal_connect(G_OBJECT(w_imgv), "key-press-event",
                     G_CALLBACK(cb_key_press_event), NULL);
    
    gtk_widget_show_all(w_top);
    gtk_image_viewer_zoom_fit(GTK_IMAGE_VIEWER(w_imgv));

    w_calibrate_dialog = giv_calibrate_dialog_new(w_imgv,
                                                  1,"",1);
    
    g_signal_connect(w_calibrate_dialog, "calib-changed",
                     G_CALLBACK(cb_calib_changed), NULL);
    g_signal_connect(w_calibrate_dialog, "delete-event",
                     G_CALLBACK(cb_calib_hide), NULL);

    gtk_widget_grab_focus(w_imgv);
    gtk_main();

    return 0;
}
