#include "config.h"

#include "gskvulkanrenderpassprivate.h"

#include "gskvulkanimageprivate.h"
#include "gskrendernodeprivate.h"
#include "gskrenderer.h"

typedef struct _GskVulkanRenderOp GskVulkanRenderOp;

typedef enum {
  GSK_VULKAN_OP_FALLBACK
} GskVulkanOpType;

struct _GskVulkanRenderOp
{
  GskVulkanOpType      type;
  GskRenderNode       *node; /* node that's the source of this op */
  GskVulkanRenderPass *pass; /* render pass required to set up node */
  GskVulkanImage      *source; /* source image to render */
  gsize                vertex_offset; /* offset into vertex buffer */
  gsize                vertex_count; /* number of vertices */
  gsize                descriptor_set_index; /* index into descriptor sets array for the right descriptor set to bind */
};

struct _GskVulkanRenderPass
{
  GdkVulkanContext *vulkan;

  GArray *render_ops;
};

GskVulkanRenderPass *
gsk_vulkan_render_pass_new (GdkVulkanContext *context)
{
  GskVulkanRenderPass *self;

  self = g_slice_new0 (GskVulkanRenderPass);
  self->vulkan = g_object_ref (context);
  self->render_ops = g_array_new (FALSE, FALSE, sizeof (GskVulkanRenderOp));

  return self;
}

void
gsk_vulkan_render_pass_free (GskVulkanRenderPass *self)
{
  g_array_unref (self->render_ops);
  g_object_unref (self->vulkan);

  g_slice_free (GskVulkanRenderPass, self);
}

void
gsk_vulkan_render_pass_add_node (GskVulkanRenderPass *self,
                                 GskVulkanRender     *render,
                                 GskRenderNode       *node)
{
  GskVulkanRenderOp op = {
    .type = GSK_VULKAN_OP_FALLBACK,
    .node = node
  };

  g_array_append_val (self->render_ops, op);
}

static void
gsk_vulkan_render_pass_upload_fallback (GskVulkanRenderPass *self,
                                        GskVulkanRenderOp   *op,
                                        GskVulkanRender     *render,
                                        VkCommandBuffer      command_buffer)
{
  graphene_rect_t bounds;
  GskRenderer *fallback;
  cairo_surface_t *surface;
  cairo_t *cr;

  gsk_render_node_get_bounds (op->node, &bounds);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        ceil (bounds.size.width),
                                        ceil (bounds.size.height));
  cr = cairo_create (surface);
  cairo_translate (cr, bounds.origin.x, bounds.origin.y);

  fallback = gsk_renderer_create_fallback (gsk_vulkan_render_get_renderer (render),
                                           &bounds,
                                           cr);
  gsk_renderer_render (fallback, op->node, NULL);
  g_object_unref (fallback);
  
  cairo_destroy (cr);

  op->source = gsk_vulkan_image_new_from_data (self->vulkan,
                                               command_buffer,
                                               cairo_image_surface_get_data (surface),
                                               cairo_image_surface_get_width (surface),
                                               cairo_image_surface_get_height (surface),
                                               cairo_image_surface_get_stride (surface));

  cairo_surface_destroy (surface);

  gsk_vulkan_render_add_cleanup_image (render, op->source);
}

void
gsk_vulkan_render_pass_upload (GskVulkanRenderPass *self,
                               GskVulkanRender     *render,
                               VkCommandBuffer      command_buffer)
{
  GskVulkanRenderOp *op;
  guint i;

  for (i = 0; i < self->render_ops->len; i++)
    {
      op = &g_array_index (self->render_ops, GskVulkanRenderOp, i);

      switch (op->type)
        {
        case GSK_VULKAN_OP_FALLBACK:
          gsk_vulkan_render_pass_upload_fallback (self, op, render, command_buffer);
          break;

        default:
          g_assert_not_reached ();
          break;
        }
    }
}

gsize
gsk_vulkan_render_pass_count_vertices (GskVulkanRenderPass *self)
{
  return self->render_ops->len * 6;
}

static gsize
gsk_vulkan_render_op_collect_vertices (GskVulkanRenderOp *op,
                                       GskVulkanVertex   *vertices)
{
  graphene_rect_t bounds;

  gsk_render_node_get_bounds (op->node, &bounds);

  vertices[0] = (GskVulkanVertex) { bounds.origin.x,                     bounds.origin.y,                      0.0, 0.0 };
  vertices[1] = (GskVulkanVertex) { bounds.origin.x + bounds.size.width, bounds.origin.y,                      1.0, 0.0 };
  vertices[2] = (GskVulkanVertex) { bounds.origin.x,                     bounds.origin.y + bounds.size.height, 0.0, 1.0 };
  vertices[3] = (GskVulkanVertex) { bounds.origin.x,                     bounds.origin.y + bounds.size.height, 0.0, 1.0 };
  vertices[4] = (GskVulkanVertex) { bounds.origin.x + bounds.size.width, bounds.origin.y,                      1.0, 0.0 };
  vertices[5] = (GskVulkanVertex) { bounds.origin.x + bounds.size.width, bounds.origin.y + bounds.size.height, 1.0, 1.0 };

  return 6;
}

gsize
gsk_vulkan_render_pass_collect_vertices (GskVulkanRenderPass *self,
                                         GskVulkanVertex     *vertices,
                                         gsize                offset,
                                         gsize                total)
{
  GskVulkanRenderOp *op;
  gsize n;
  guint i;

  n = 0;
  for (i = 0; i < self->render_ops->len; i++)
    {
      op = &g_array_index (self->render_ops, GskVulkanRenderOp, i);

      switch (op->type)
        {
        case GSK_VULKAN_OP_FALLBACK:
          op->vertex_offset = offset + n;
          op->vertex_count = gsk_vulkan_render_op_collect_vertices (op, vertices + n + offset);
          break;

        default:
          g_assert_not_reached ();
          break;
        }

      n += op->vertex_count;
      g_assert (n + offset <= total);
    }

  return n;
}

void
gsk_vulkan_render_pass_reserve_descriptor_sets (GskVulkanRenderPass *self,
                                                GskVulkanRender     *render)
{
  GskVulkanRenderOp *op;
  guint i;

  for (i = 0; i < self->render_ops->len; i++)
    {
      op = &g_array_index (self->render_ops, GskVulkanRenderOp, i);

      switch (op->type)
        {
        case GSK_VULKAN_OP_FALLBACK:
          op->descriptor_set_index = gsk_vulkan_render_reserve_descriptor_set (render, op->source);
          break;

        default:
          g_assert_not_reached ();
          break;
        }
    }
}

void
gsk_vulkan_render_pass_draw (GskVulkanRenderPass *self,
                             GskVulkanRender     *render,
                             GskVulkanPipeline   *pipeline,
                             VkCommandBuffer      command_buffer)
{
  GskVulkanRenderOp *op;
  guint i;

  for (i = 0; i < self->render_ops->len; i++)
    {
      op = &g_array_index (self->render_ops, GskVulkanRenderOp, i);

      vkCmdBindDescriptorSets (command_buffer,
                               VK_PIPELINE_BIND_POINT_GRAPHICS,
                               gsk_vulkan_pipeline_get_pipeline_layout (pipeline),
                               0,
                               1,
                               (VkDescriptorSet[1]) {
                                   gsk_vulkan_render_get_descriptor_set (render, op->descriptor_set_index)
                               },
                               0,
                               NULL);

      vkCmdDraw (command_buffer,
                 op->vertex_count, 1,
                 op->vertex_offset, 0);
    }
}