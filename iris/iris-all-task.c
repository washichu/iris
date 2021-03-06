/* iris-all-task.c
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

#include "iris-task.h"
#include "iris-task-private.h"

/**
 * iris_task_vall_of:
 * @first_task: An #IrisTask
 *
 * Creates a new task that will complete when each of the passed
 * #IrisTask<!-- -->'s complete.
 *
 * Return value: the newly created #IrisTask instance.
 */
IrisTask*
iris_task_vall_of (IrisTask *first_task, ...)
{
	IrisTask *task;
	IrisTask *iter;
	va_list   args;

	if (!first_task)
		return NULL;

	task = iris_task_new ();
	iter = first_task;
	va_start (args, first_task);

	while (iter) {
		if (IRIS_IS_TASK (iter))
			iris_task_add_dependency (task, iter);
		iter = va_arg (args, IrisTask*);
	}

	va_end (args);

	return task;
}

/**
 * iris_task_all_of:
 * @first_task: An #IrisTask
 *
 * Creates a new task that will complete when each of the passed
 * #IrisTask<!-- -->'s complete.
 *
 * Return value: the newly created #IrisTask instance.
 */
IrisTask*
iris_task_all_of (GList *tasks)
{
	IrisTask *task;

	g_return_val_if_fail (tasks != NULL, NULL);

	task = iris_task_new ();
	for (; tasks; tasks = tasks->next)
		iris_task_add_dependency (task, tasks->data);

	return task;
}
