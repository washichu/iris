#include <iris/iris.h>
#include <iris/iris-wsqueue-private.h>

static void
test1 (void)
{
	IrisQueue  *queue;
	IrisQueue  *global;

	global = iris_queue_new ();
	queue = iris_wsqueue_new (global, iris_rrobin_new (1));
	g_assert (queue);
}

static void
test4 (void)
{
	IrisQueue *queue;
	gint i = 0;

	queue = iris_wsqueue_new (iris_queue_new (), iris_rrobin_new (1));
	iris_wsqueue_local_push (IRIS_WSQUEUE (queue), &i);
	g_assert (iris_wsqueue_local_pop (IRIS_WSQUEUE (queue)) == &i);
}

static void
test5 (void)
{
	IrisQueue *queue;
	gint i = 0;

	queue = iris_wsqueue_new (iris_queue_new (), iris_rrobin_new (1));
	iris_wsqueue_local_push (IRIS_WSQUEUE (queue), &i);
	g_assert (iris_wsqueue_try_steal (IRIS_WSQUEUE (queue), 0) == &i);
}

static void
test6 (void)
{
	IrisQueue  *queue;
	IrisQueue  *global;
	IrisQueue  *neighbor;
	IrisRRobin *rrobin;

	global = iris_queue_new ();
	rrobin = iris_rrobin_new (2);
	neighbor = iris_wsqueue_new (global, rrobin);
	queue = iris_wsqueue_new (global, rrobin);

	iris_rrobin_append (rrobin, queue);
	iris_rrobin_append (rrobin, neighbor);

	iris_wsqueue_local_push (((IrisWSQueue*)queue), GINT_TO_POINTER (1));
	iris_queue_push (global, GINT_TO_POINTER (2));
	iris_wsqueue_local_push (((IrisWSQueue*)neighbor), GINT_TO_POINTER (3));

	g_assert_cmpint (GPOINTER_TO_INT (iris_queue_pop (queue)), ==, 1);
	g_assert_cmpint (GPOINTER_TO_INT (iris_queue_pop (queue)), ==, 2);
	g_assert_cmpint (GPOINTER_TO_INT (iris_queue_pop (queue)), ==, 3);
}

static void
test7 (void)
{
	IrisQueue *queue;

	queue = iris_wsqueue_new (iris_queue_new (), iris_rrobin_new (1));
	g_assert (queue);

	iris_wsqueue_local_push (IRIS_WSQUEUE (queue), GINT_TO_POINTER (1));

	g_assert_cmpint (GPOINTER_TO_INT (iris_queue_try_pop (queue)), ==, 1);
}

static void
test8 (void)
{
	IrisQueue *queue;
	GTimeVal   tv = {0,0};

	queue = iris_wsqueue_new (iris_queue_new (), iris_rrobin_new (1));
	g_assert (queue);

	iris_wsqueue_local_push (IRIS_WSQUEUE (queue), GINT_TO_POINTER (1));

	g_get_current_time (&tv);
	g_assert_cmpint (GPOINTER_TO_INT (iris_queue_timed_pop (queue, &tv)), ==, 1);
}

static void
test9 (void)
{
	IrisQueue *queue;
	gint i;

	queue = iris_wsqueue_new (iris_queue_new (), iris_rrobin_new (1));
	g_assert (queue);

	for (i = 0; i < 1025; i++) {
		iris_wsqueue_local_push (IRIS_WSQUEUE (queue), &i);
	}
}

static void
test10 (void)
{
	g_assert_cmpint (IRIS_TYPE_WSQUEUE, !=, G_TYPE_INVALID);
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/wsqueue/new", test1);
	g_test_add_func ("/wsqueue/local1", test4);
	g_test_add_func ("/wsqueue/try_steal1", test5);
	g_test_add_func ("/wsqueue/pop_order1", test6);
	g_test_add_func ("/wsqueue/try_pop1", test7);
	g_test_add_func ("/wsqueue/timed_pop1", test8);
	g_test_add_func ("/wsqueue/many_push1", test9);
	g_test_add_func ("/wsqueue/get_type", test10);

	return g_test_run ();
}
