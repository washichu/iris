/* iris-stack-private.h
 *
 * Copyright (C) 2009 Christian Hergert <chris@dronelabs.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 
 * 02110-1301 USA
 */

#ifndef __IRIS_STACK_PRIVATE_H__
#define __IRIS_STACK_PRIVATE_H__

#include "iris-link.h"
#include "iris-free-list.h"

G_BEGIN_DECLS

struct _IrisStack
{
	/*< private >*/
	IrisLink      *head;
	IrisFreeList  *free_list;
	volatile gint  ref_count;
};

G_END_DECLS

#endif /* __IRIS_STACK_PRIVATE_H__ */
