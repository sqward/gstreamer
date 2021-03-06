/*
 * collectpads.c - GstCollectPads testsuite
 * Copyright (C) 2006 Alessandro Decina <alessandro@nnva.org>
 *
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/check/gstcheck.h>
#include <gst/base/gstcollectpads.h>

#define fail_unless_collected(expected)           \
G_STMT_START {                                    \
  g_mutex_lock (&lock);                           \
  while (expected == TRUE && collected == FALSE)  \
    g_cond_wait (&cond,& lock);                   \
  fail_unless_equals_int (collected, expected);   \
  g_mutex_unlock (&lock);                         \
} G_STMT_END;

typedef struct
{
  char foo;
} BadCollectData;

typedef struct
{
  GstCollectData data;
  GstPad *pad;
  GstBuffer *buffer;
  GstEvent *event;
} TestData;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstCollectPads *collect;
static gboolean collected;
static GstPad *srcpad1, *srcpad2;
static GstPad *sinkpad1, *sinkpad2;
static TestData *data1, *data2;

static GMutex lock;
static GCond cond;

static GstFlowReturn
collected_cb (GstCollectPads * pads, gpointer user_data)
{
  g_mutex_lock (&lock);
  collected = TRUE;
  g_cond_signal (&cond);
  g_mutex_unlock (&lock);

  return GST_FLOW_OK;
}

static GstFlowReturn
handle_buffer_cb (GstCollectPads * pads, GstCollectData * data,
    GstBuffer * buf, gpointer user_data)
{
  GST_DEBUG ("Collected a buffer via callback");
  g_mutex_lock (&lock);
  collected = TRUE;
  g_cond_signal (&cond);
  g_mutex_unlock (&lock);

  return GST_FLOW_OK;
}

static gpointer
push_buffer (gpointer user_data)
{
  GstFlowReturn flow;
  GstCaps *caps;
  TestData *test_data = (TestData *) user_data;

  gst_pad_push_event (test_data->pad, gst_event_new_stream_start ("test"));

  caps = gst_caps_new_empty_simple ("foo/x-bar");
  gst_pad_push_event (test_data->pad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  flow = gst_pad_push (test_data->pad, test_data->buffer);
  fail_unless (flow == GST_FLOW_OK, "got flow %s instead of OK",
      gst_flow_get_name (flow));

  return NULL;
}

static gpointer
push_event (gpointer user_data)
{
  TestData *test_data = (TestData *) user_data;

  fail_unless (gst_pad_push_event (test_data->pad, test_data->event) == TRUE);

  return NULL;
}

static void
setup_default (void)
{
  collect = gst_collect_pads_new ();

  srcpad1 = gst_pad_new_from_static_template (&srctemplate, "src1");
  srcpad2 = gst_pad_new_from_static_template (&srctemplate, "src2");
  sinkpad1 = gst_pad_new_from_static_template (&sinktemplate, "sink1");
  sinkpad2 = gst_pad_new_from_static_template (&sinktemplate, "sink2");
  fail_unless (gst_pad_link (srcpad1, sinkpad1) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_link (srcpad2, sinkpad2) == GST_PAD_LINK_OK);

  gst_pad_set_active (sinkpad1, TRUE);
  gst_pad_set_active (sinkpad2, TRUE);
  gst_pad_set_active (srcpad1, TRUE);
  gst_pad_set_active (srcpad2, TRUE);

  data1 = NULL;
  data2 = NULL;
  collected = FALSE;
}

static void
setup (void)
{
  setup_default ();
  gst_collect_pads_set_function (collect, collected_cb, NULL);
}

static void
setup_buffer_cb (void)
{
  setup_default ();
  gst_collect_pads_set_buffer_function (collect, handle_buffer_cb, NULL);
}

static void
teardown (void)
{
  gst_object_unref (sinkpad1);
  gst_object_unref (sinkpad2);
  gst_object_unref (collect);
}

GST_START_TEST (test_pad_add_remove)
{
  ASSERT_CRITICAL (gst_collect_pads_add_pad (collect, sinkpad1,
          sizeof (BadCollectData), NULL, TRUE));

  data1 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad1, sizeof (TestData), NULL, TRUE);
  fail_unless (data1 != NULL);

  fail_unless (gst_collect_pads_remove_pad (collect, sinkpad2) == FALSE);
  fail_unless (gst_collect_pads_remove_pad (collect, sinkpad1) == TRUE);
}

GST_END_TEST;

GST_START_TEST (test_collect)
{
  GstBuffer *buf1, *buf2, *tmp;
  GThread *thread1, *thread2;

  data1 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad1, sizeof (TestData), NULL, TRUE);
  fail_unless (data1 != NULL);

  data2 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad2, sizeof (TestData), NULL, TRUE);
  fail_unless (data2 != NULL);

  buf1 = gst_buffer_new ();
  buf2 = gst_buffer_new ();

  /* start collect pads */
  gst_collect_pads_start (collect);

  /* push buffers on the pads */
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);
  /* here thread1 is blocked and srcpad1 has a queued buffer */
  fail_unless_collected (FALSE);

  data2->pad = srcpad2;
  data2->buffer = buf2;
  thread2 = g_thread_try_new ("gst-check", push_buffer, data2, NULL);

  /* now both pads have a buffer */
  fail_unless_collected (TRUE);

  tmp = gst_collect_pads_pop (collect, (GstCollectData *) data1);
  fail_unless (tmp == buf1);
  tmp = gst_collect_pads_pop (collect, (GstCollectData *) data2);
  fail_unless (tmp == buf2);

  /* these will return immediately as at this point the threads have been
   * unlocked and are finished */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_collect_pads_stop (collect);

  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);
}

GST_END_TEST;


GST_START_TEST (test_collect_eos)
{
  GstBuffer *buf1, *tmp;
  GThread *thread1, *thread2;

  data1 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad1, sizeof (TestData), NULL, TRUE);
  fail_unless (data1 != NULL);

  data2 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad2, sizeof (TestData), NULL, TRUE);
  fail_unless (data2 != NULL);

  buf1 = gst_buffer_new ();

  /* start collect pads */
  gst_collect_pads_start (collect);

  /* push a buffer on srcpad1 and EOS on srcpad2 */
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);
  /* here thread1 is blocked and srcpad1 has a queued buffer */
  fail_unless_collected (FALSE);

  data2->pad = srcpad2;
  data2->event = gst_event_new_eos ();
  thread2 = g_thread_try_new ("gst-check", push_event, data2, NULL);
  /* now sinkpad1 has a buffer and sinkpad2 has EOS */
  fail_unless_collected (TRUE);

  tmp = gst_collect_pads_pop (collect, (GstCollectData *) data1);
  fail_unless (tmp == buf1);
  /* sinkpad2 has EOS so a NULL buffer is returned */
  tmp = gst_collect_pads_pop (collect, (GstCollectData *) data2);
  fail_unless (tmp == NULL);

  /* these will return immediately as when the data is popped the threads are
   * unlocked and will terminate */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_collect_pads_stop (collect);

  gst_buffer_unref (buf1);
}

GST_END_TEST;

GST_START_TEST (test_collect_twice)
{
  GstBuffer *buf1, *buf2, *tmp;
  GThread *thread1, *thread2;

  data1 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad1, sizeof (TestData), NULL, TRUE);
  fail_unless (data1 != NULL);

  data2 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad2, sizeof (TestData), NULL, TRUE);
  fail_unless (data2 != NULL);

  GST_INFO ("round 1");

  buf1 = gst_buffer_new ();

  /* start collect pads */
  gst_collect_pads_start (collect);

  /* queue a buffer */
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);
  /* here thread1 is blocked and srcpad1 has a queued buffer */
  fail_unless_collected (FALSE);

  /* push EOS on the other pad */
  data2->pad = srcpad2;
  data2->event = gst_event_new_eos ();
  thread2 = g_thread_try_new ("gst-check", push_event, data2, NULL);

  /* one of the pads has a buffer, the other has EOS */
  fail_unless_collected (TRUE);

  tmp = gst_collect_pads_pop (collect, (GstCollectData *) data1);
  fail_unless (tmp == buf1);
  /* there's nothing to pop from the one which received EOS */
  tmp = gst_collect_pads_pop (collect, (GstCollectData *) data2);
  fail_unless (tmp == NULL);

  /* these will return immediately as at this point the threads have been
   * unlocked and are finished */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_collect_pads_stop (collect);
  collected = FALSE;

  GST_INFO ("round 2");

  buf2 = gst_buffer_new ();

  /* clear EOS from pads */
  gst_pad_push_event (srcpad1, gst_event_new_flush_stop (TRUE));
  gst_pad_push_event (srcpad2, gst_event_new_flush_stop (TRUE));

  /* start collect pads */
  gst_collect_pads_start (collect);

  /* push buffers on the pads */
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);
  /* here thread1 is blocked and srcpad1 has a queued buffer */
  fail_unless_collected (FALSE);

  data2->pad = srcpad2;
  data2->buffer = buf2;
  thread2 = g_thread_try_new ("gst-check", push_buffer, data2, NULL);

  /* now both pads have a buffer */
  fail_unless_collected (TRUE);

  tmp = gst_collect_pads_pop (collect, (GstCollectData *) data1);
  fail_unless (tmp == buf1);
  tmp = gst_collect_pads_pop (collect, (GstCollectData *) data2);
  fail_unless (tmp == buf2);

  /* these will return immediately as at this point the threads have been
   * unlocked and are finished */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_collect_pads_stop (collect);

  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);

}

GST_END_TEST;


/* Test the default collected buffer func */
GST_START_TEST (test_collect_default)
{
  GstBuffer *buf1, *buf2, *tmp;
  GThread *thread1, *thread2;

  data1 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad1, sizeof (TestData), NULL, TRUE);
  fail_unless (data1 != NULL);

  data2 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad2, sizeof (TestData), NULL, TRUE);
  fail_unless (data2 != NULL);

  buf1 = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buf1) = 0;
  buf2 = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buf2) = GST_SECOND;

  /* start collect pads */
  gst_collect_pads_start (collect);

  /* push buffers on the pads */
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);
  /* here thread1 is blocked and srcpad1 has a queued buffer */
  fail_unless_collected (FALSE);

  data2->pad = srcpad2;
  data2->buffer = buf2;
  thread2 = g_thread_try_new ("gst-check", push_buffer, data2, NULL);

  /* now both pads have a buffer */
  fail_unless_collected (TRUE);

  /* The default callback should have popped the buffer with lower timestamp,
   * and this should therefore be NULL: */
  tmp = gst_collect_pads_pop (collect, (GstCollectData *) data1);
  fail_unless (tmp == NULL);
  /* While this one should still be pending: */
  tmp = gst_collect_pads_pop (collect, (GstCollectData *) data2);
  fail_unless (tmp == buf2);

  /* these will return immediately as at this point the threads have been
   * unlocked and are finished */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_collect_pads_stop (collect);

  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);
}

GST_END_TEST;

static Suite *
gst_collect_pads_suite (void)
{
  Suite *suite;
  TCase *general, *buffers;

  suite = suite_create ("GstCollectPads");
  general = tcase_create ("general");
  suite_add_tcase (suite, general);
  tcase_add_checked_fixture (general, setup, teardown);
  tcase_add_test (general, test_pad_add_remove);
  tcase_add_test (general, test_collect);
  tcase_add_test (general, test_collect_eos);
  tcase_add_test (general, test_collect_twice);

  buffers = tcase_create ("buffers");
  suite_add_tcase (suite, buffers);
  tcase_add_checked_fixture (buffers, setup_buffer_cb, teardown);
  tcase_add_test (buffers, test_collect_default);

  return suite;
}

GST_CHECK_MAIN (gst_collect_pads);
