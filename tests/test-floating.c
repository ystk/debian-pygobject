/*
 * test-floating.c - Source for TestFloatingWithSinkFunc and TestFloatingWithoutSinkFunc
 * Copyright (C) 2010 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "test-floating.h"

/* TestFloatingWithSinkFunc */

G_DEFINE_TYPE(TestFloatingWithSinkFunc, test_floating_with_sink_func, G_TYPE_INITIALLY_UNOWNED)

static void
test_floating_with_sink_func_finalize (GObject *gobject)
{
  TestFloatingWithSinkFunc *object = TEST_FLOATING_WITH_SINK_FUNC (gobject);

  if (g_object_is_floating (object))
    {
      g_warning ("A floating object was finalized. This means that someone\n"
		 "called g_object_unref() on an object that had only a floating\n"
		 "reference; the initial floating reference is not owned by anyone\n"
		 "and must be removed with g_object_ref_sink().");
    }
  
  G_OBJECT_CLASS (test_floating_with_sink_func_parent_class)->finalize (gobject);
}

static void
test_floating_with_sink_func_class_init (TestFloatingWithSinkFuncClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = test_floating_with_sink_func_finalize;
}

static void
test_floating_with_sink_func_init (TestFloatingWithSinkFunc *self)
{
}

void
sink_test_floating_with_sink_func (GObject *object)
{
    if (g_object_is_floating(object)) {
	    g_object_ref_sink(object);
    }
}

/* TestFloatingWithoutSinkFunc */

G_DEFINE_TYPE(TestFloatingWithoutSinkFunc, test_floating_without_sink_func, G_TYPE_INITIALLY_UNOWNED)

static void
test_floating_without_sink_func_finalize (GObject *gobject)
{
  TestFloatingWithoutSinkFunc *object = TEST_FLOATING_WITHOUT_SINK_FUNC (gobject);

  if (g_object_is_floating (object))
    {
      g_warning ("A floating object was finalized. This means that someone\n"
		 "called g_object_unref() on an object that had only a floating\n"
		 "reference; the initial floating reference is not owned by anyone\n"
		 "and must be removed without g_object_ref_sink().");
    }

  G_OBJECT_CLASS (test_floating_without_sink_func_parent_class)->finalize (gobject);
}

static void
test_floating_without_sink_func_class_init (TestFloatingWithoutSinkFuncClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = test_floating_without_sink_func_finalize;
}

static void
test_floating_without_sink_func_init (TestFloatingWithoutSinkFunc *self)
{
}

