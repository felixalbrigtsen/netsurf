/*
 * Copyright 2013 Stephen Fryatt <stevef@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


/** \file mouse.h
 * Mouse dragging and tracking support (interface).
 */

#ifndef _NETSURF_RISCOS_MOUSE_H_
#define _NETSURF_RISCOS_MOUSE_H_

enum ro_mouse_track_response {
	RO_MOUSE_RESPONSE_LOW,
	RO_MOUSE_RESPONSE_HIGH
};


/**
 * Process Null polls for any drags and mouse trackers that are currently
 * active.
 */

void ro_mouse_poll(void);


/**
 * Start a drag, providing a function to be called when the Wimp_DragEnd event
 * is received and optionally a tracking function to be called on null polls
 * in between times.
 *
 * \param *drag_end	Callback for when the drag terminates, or NULL for none.
 * \param *drag_track	Callback for mouse tracking during the drag, or NULL for
 *			none.
 * \param *drag_cancel	Callback for cancelling the drag, or NULL if the drag
 *			can't be cancelled.
 * \param *data		Data to be passed to the callback functions, or NULL.
 */
 
void ro_mouse_drag_start(void (*drag_end)(wimp_dragged *dragged, void *data),
		void (*drag_track)(wimp_pointer *pointer, void *data),
		void (*drag_cancel)(void *data), void *data);


/**
 * Process Wimp_DragEnd events by passing the details on to any registered
 * event handler.
 *
 * \param *dragged	The Wimp_DragEnd data block.
 */

void ro_mouse_drag_end(wimp_dragged *dragged);

#endif

