/* iris-receiver.c
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

#include "iris-arbiter.h"
#include "iris-arbiter-private.h"
#include "iris-debug.h"
#include "iris-receiver.h"
#include "iris-receiver-private.h"
#include "iris-port.h"

/**
 * SECTION:iris-receiver
 * @title: IrisReceiver
 * @short_description: Perform actions upon message delivery
 *
 * #IrisReceiver is used to perform actions when a message is received
 * from an #IrisPort.  See iris_arbiter_receive() for how to create
 * a new receiver for an #IrisPort to perform a given action.
 *
 * #IrisReceiver<!-- -->'s can be attached to an arbiter to provide
 * additional control over when actions can be performed.  See
 * iris_arbiter_coordinate() for how to use the Coordination Arbiter.
 * It provides a feature similar to a ReaderWriter lock using an
 * asynchronous model.
 */

G_DEFINE_TYPE (IrisReceiver, iris_receiver, G_TYPE_OBJECT)

typedef struct
{
	IrisReceiver *receiver;
	IrisMessage  *message;
} IrisWorkerData;

GType
iris_delivery_status_get_type (void)
{
	static GType      gtype    = 0;
	static GEnumValue values[] = {
		{ IRIS_DELIVERY_ACCEPTED,        "IRIS_DELIVERY_ACCEPTED",        "ACCEPTED" },
		{ IRIS_DELIVERY_PAUSE,           "IRIS_DELIVERY_PAUSE",           "PAUSE" },
		{ IRIS_DELIVERY_ACCEPTED_PAUSE,  "IRIS_DELIVERY_ACCEPTED_PAUSE",  "ACCEPTED_PAUSE" },
		{ IRIS_DELIVERY_REMOVE,          "IRIS_DELIVERY_REMOVE",          "REMOVE" },
		{ IRIS_DELIVERY_ACCEPTED_REMOVE, "IRIS_DELIVERY_ACCEPTED_REMOVE", "ACCEPTED_REMOVE" },
		{ 0, NULL, NULL }
	};

	if (G_UNLIKELY (!gtype))
		gtype = g_enum_register_static ("IrisDeliveryStatus", values);

	return gtype;
}

static void
iris_receiver_worker_cb (gpointer data)
{
	IrisReceiverPrivate *priv;
	IrisWorkerData      *worker;

	g_return_if_fail (data != NULL);

	worker = data;
	priv = worker->receiver->priv;

	if (g_atomic_int_dec_and_test (&priv->active)) {}
	iris_message_unref (worker->message);
	g_slice_free (IrisWorkerData, worker);
}

static void
iris_receiver_worker (gpointer data)
{
	IrisReceiverPrivate *priv;
	IrisWorkerData      *worker;

	g_return_if_fail (data != NULL);

	worker = data;
	priv = worker->receiver->priv;

	/* Execute the callback */
	priv->callback (worker->message, priv->data);

	/* notify the arbiter we are complete */
	if (priv->arbiter)
		iris_arbiter_receive_completed (priv->arbiter,
		                                worker->receiver);
}

static IrisDeliveryStatus
iris_receiver_deliver_real (IrisReceiver *receiver,
                            IrisMessage  *message)
{
	IrisReceiverPrivate *priv;
	IrisDeliveryStatus   status = IRIS_DELIVERY_ACCEPTED;
	IrisReceiveDecision  decision;
	gboolean             execute = TRUE;
	gboolean             unpause = FALSE;
	IrisWorkerData      *worker;

	g_return_val_if_fail (message != NULL, IRIS_DELIVERY_REMOVE);

	priv = receiver->priv;

	/* arbiter cannot be changed after instantiation, so it is safe to
	 * check the arbiter pointer with out a lock or memory barrier.
	 * Without an arbiter, we cannot pause, so we can assume that the
	 * flood gates are open if we do not have a max allowed or an
	 * arbiter. (Fast-Path)
	 */
	if (!priv->arbiter && !priv->max_active) {
		execute = TRUE;
		status = IRIS_DELIVERY_ACCEPTED;
		goto _post_decision;
	}

	g_static_rec_mutex_lock (&priv->mutex);

	if (g_atomic_int_get (&priv->completed) == TRUE) {
		decision = IRIS_DELIVERY_REMOVE;
		execute = FALSE;
	}
	else if ((priv->max_active > 0 && priv->active == priv->max_active) || g_atomic_pointer_get (&priv->message))
	{
		/* We cannot accept an item at this time, so let
		 * the port queue the item for us.
		 */
		decision = IRIS_DELIVERY_PAUSE;
		execute = FALSE;
	}
	else if (priv->arbiter) {
		decision = iris_arbiter_can_receive (priv->arbiter, receiver);

		switch (decision) {
		case IRIS_RECEIVE_NOW:
			/* We can execute this now */
			execute = TRUE;
			status = IRIS_DELIVERY_ACCEPTED;
			/* We also need to unpause the port */
			unpause = TRUE;
			break;
		case IRIS_RECEIVE_LATER:
			/* We queue this item ourselves */
			execute = FALSE;
			priv->message = iris_message_ref (message);
			status = IRIS_DELIVERY_ACCEPTED_PAUSE;
			break;
		case IRIS_RECEIVE_NEVER:
			/* The port should queue the item and remove us */
			execute = FALSE;
			status = IRIS_DELIVERY_REMOVE;
			break;
		default:
			g_warn_if_reached ();
			break;
		}
	}

	g_static_rec_mutex_unlock (&priv->mutex);

_post_decision:

	/* We do this before leaving the lock to prevent a potential
	 * race condition where we could go over our max concurrent.
	 */
	if (execute)
		g_atomic_int_inc (&priv->active);

	/* If our execution will be the only execution allowed, so make
	 * sure that we mark the receiver as it is completed.  Also, we
	 * need to compare exchange just incase we race and lose.  This
	 * could happen if we are not persistent and do not have an arbiter,
	 * which means we avoid the lock above.
	 */
	if (!priv->persistent && execute)
		if (!g_atomic_int_compare_and_exchange (&priv->completed, FALSE, TRUE))
			execute = FALSE;

	if (execute) {
		worker = g_slice_new0 (IrisWorkerData);
		worker->receiver = receiver;
		worker->message = iris_message_ref (message);

		iris_scheduler_queue (priv->scheduler,
		                      iris_receiver_worker,
		                      worker,
		                      iris_receiver_worker_cb);

		if (!priv->persistent)
			status = IRIS_DELIVERY_ACCEPTED_REMOVE;
	}

	if (unpause && iris_port_is_paused (priv->port))
		iris_port_flush (priv->port);

	return status;
}

static void
iris_receiver_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_receiver_parent_class)->finalize (object);
}

static void
iris_receiver_dispose (GObject *object)
{
	IrisReceiverPrivate *priv;

	g_return_if_fail (IRIS_IS_RECEIVER (object));

	priv = IRIS_RECEIVER (object)->priv;

	g_object_unref (priv->port);
	g_object_unref (priv->scheduler);

	if (priv->notify)
		priv->notify (priv->data);

	G_OBJECT_CLASS (iris_receiver_parent_class)->dispose (object);
}

static void
iris_receiver_class_init (IrisReceiverClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	klass->deliver = iris_receiver_deliver_real;
	object_class->finalize = iris_receiver_finalize;
	object_class->dispose = iris_receiver_dispose;

	g_type_class_add_private (object_class, sizeof (IrisReceiverPrivate));
}

static void
iris_receiver_init (IrisReceiver *receiver)
{
	receiver->priv = G_TYPE_INSTANCE_GET_PRIVATE (receiver,
	                                              IRIS_TYPE_RECEIVER,
	                                              IrisReceiverPrivate);

	g_static_rec_mutex_init (&receiver->priv->mutex);
	receiver->priv->persistent = TRUE;
}

/**
 * iris_receiver_deliver:
 * @receiver: An #IrisReceiver
 * @message: An #IrisMessage
 *
 * Delivers a message to the receiver so that the receiver may take an
 * action on the message.
 *
 * Return value: the status code for the delivery.
 */
IrisDeliveryStatus
iris_receiver_deliver (IrisReceiver *receiver,
                       IrisMessage  *message)
{
	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), IRIS_DELIVERY_REMOVE);
	return IRIS_RECEIVER_GET_CLASS (receiver)->deliver (receiver, message);
}

/**
 * iris_receiver_has_arbiter:
 * @receiver: An #IrisReceiver
 *
 * Private, used by unit tests and internally only.
 *
 * Determines if the receiver currently has an arbiter attached.
 *
 * Return value: TRUE if an arbiter exists.
 */
gboolean
iris_receiver_has_arbiter (IrisReceiver *receiver)
{
	IrisReceiverPrivate *priv;

	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), FALSE);

	priv = receiver->priv;

	return g_atomic_pointer_get (&priv->arbiter) != NULL;
}

/**
 * iris_receiver_has_scheduler:
 * @receiver: An #IrisReceiver
 *
 * Private, used by unit tests and internally only.
 *
 * Determines if the receiver currently has a scheduler attached.
 *
 * Return value: TRUE if a scheduler exists.
 */
gboolean
iris_receiver_has_scheduler (IrisReceiver *receiver)
{
	IrisReceiverPrivate *priv;

	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), FALSE);

	priv = receiver->priv;

	return g_atomic_pointer_get (&priv->scheduler) != NULL;
}

/**
 * iris_receiver_get_scheduler:
 * @receiver: An #IrisReceiver
 *
 * Retrieves the scheduler instance for the receiver.
 *
 * Return value: An #IrisScheduler instance
 */
IrisScheduler*
iris_receiver_get_scheduler (IrisReceiver *receiver)
{
	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), NULL);
	return g_atomic_pointer_get (&receiver->priv->scheduler);
}

/**
 * iris_receiver_set_scheduler:
 * @receiver: An #IrisReceiver
 * @scheduler: An #IrisScheduler
 *
 * Sets the scheduler instance used by this receiver to execute work items.
 * Note that it is probably not a good idea to switch schedulers while
 * executing work items.  However, we do make an attempt to support it.
 */
void
iris_receiver_set_scheduler (IrisReceiver  *receiver,
                             IrisScheduler *scheduler)
{
	IrisScheduler *old_sched;

	g_return_if_fail (IRIS_IS_RECEIVER (receiver));
	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));

	scheduler = g_object_ref (scheduler);

	do {
		old_sched = iris_receiver_get_scheduler (receiver);
	} while (!g_atomic_pointer_compare_and_exchange (
				(gpointer*)&receiver->priv->scheduler,
				old_sched,
				scheduler));

	g_object_unref (old_sched);
}

/**
 * iris_receiver_resume:
 * @receiver: An #IrisReceiver
 *
 * Resumes delivery of a port. Any pending messages in the receiver
 * will attempt to redeliver.  The port attached to the receiver will
 * also be flushed.
 */
void
iris_receiver_resume (IrisReceiver *receiver)
{
	IrisReceiverPrivate *priv;
	IrisMessage         *message = NULL;

	iris_debug (IRIS_DEBUG_RECEIVER);

	g_return_if_fail (IRIS_IS_RECEIVER (receiver));

	priv = receiver->priv;

	g_static_rec_mutex_lock (&priv->mutex);

	if (priv->message) {
		message = priv->message;
		priv->message = NULL;
	}

	g_static_rec_mutex_unlock (&priv->mutex);

	if (message) {
		/* repost message so it is at beginning of queue */
		iris_port_repost (priv->port, message);
		iris_message_unref (message);
	}

	iris_port_flush (priv->port);
}
