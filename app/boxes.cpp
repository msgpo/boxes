#include <gl/global.hpp>
#include <gl/buffer.hpp>
#include <gl/shader.hpp>
#include <gl/vertex_array.hpp>
#include <gl/texture.hpp>
#include <gl/mesh.hpp>
#include <gl/framebuffer.hpp>
#include <gl/scene.hpp>
#include <memory>
#include <cstdint>

using namespace std;
using namespace glm;
using namespace GL;
using namespace Util;

class Scene
{
   public:
      void init()
      {
         auto mesh = create_mesh_box();
         drawable.arrays.setup(mesh.arrays, &drawable.vert, &drawable.elem);
         drawable.vert.init(GL_ARRAY_BUFFER, mesh.vbo, Buffer::None);
         drawable.elem.init(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo, Buffer::None);
         drawable.indices = mesh.ibo.size();

         MaterialBuffer material(mesh.material);
         drawable.material.init(GL_UNIFORM_BUFFER, sizeof(material),
               Buffer::None, &material, Shader::Material);

         if (!mesh.material.diffuse_map.empty())
         {
            drawable.use_diffuse = true;
            drawable.tex.load_texture_2d({Texture::Texture2D,
                  { mesh.material.diffuse_map },
                  true });
         }
         else
            drawable.use_diffuse = false;

         drawable.shader = &shader;

         shader.set_samplers({{ "Diffuse", 0 }});
         shader.set_uniform_buffers({{ "ModelTransform", Shader::ModelTransform }, { "Material", Shader::Material }});
         shader.reserve_define("DIFFUSE_MAP", 1);
         shader.reserve_define("INSTANCED", 1);
         shader.set_define("INSTANCED", 0);
         shader.init("app/shaders/generic.vs", "app/shaders/generic.fs");
      }

      void render(const mat4& view_proj)
      {
         drawable.view_proj = view_proj;
         queue.set_view_proj(view_proj);
         queue.begin();
         queue.push(&drawable);
         queue.end();
         queue.render();
      }

   private:
      struct Block
      {
         vec4 model;
      };

      struct Drawable : Renderable
      {
         Shader *shader;
         VertexArray arrays;
         Buffer vert;
         Buffer elem;
         size_t indices;

         Buffer model;
         Buffer material;

         Texture tex;
         bool use_diffuse;
         float cache_depth;

         vector<Block> blocks;
         AABB aabb;
         mat4 view_proj;

         Drawable()
         {
            for (int z = -100; z <= 100; z += 4)
               for (int y = -100; y <= 100; y += 4)
                  for (int x = -100; x <= 100; x += 4)
                     blocks.push_back({vec4(x, y, z, 1.0f)});
            aabb.base = vec3(-101);
            aabb.offset = vec3(101) - aabb.base;

            model.init(GL_UNIFORM_BUFFER, Shader::MaxInstances * sizeof(mat4), Buffer::WriteOnly, nullptr, Shader::ModelTransform);
         }

         inline void set_cache_depth(float depth) override { cache_depth = depth; }
         inline const AABB& get_aabb() const override { return aabb; }
         inline vec4 get_model_transform() const override { return vec4(0.0f, 0.0f, 0.0f, 1.0f); }
         inline bool compare_less(const Renderable& o_tmp) const override
         {
            const Drawable& o = static_cast<const Drawable&>(o_tmp);
            if (&o == this)
               return false;

            if (shader != o.shader)
               return true;
            if (use_diffuse && !o.use_diffuse)
               return true;
            if (cache_depth < o.cache_depth)
               return true;

            return false;
         }

         inline void render()
         {
            Sampler::bind(0, Sampler::TrilinearClamp);
            shader->use();

            arrays.bind();
            material.bind();

            shader->set_define("INSTANCED", 1);
            if (use_diffuse)
            {
               tex.bind(0);
               shader->set_define("DIFFUSE_MAP", 1);
            }
            else
               shader->set_define("DIFFUSE_MAP", 0);

            vector<Block> culled_blocks;
            remove_copy_if(begin(blocks), end(blocks), back_inserter(culled_blocks),
                  [this](const Block& block) -> bool {
                     AABB aabb;
                     aabb.base = vec3(block.model.x, block.model.y, block.model.z) - vec3(1.0f);
                     aabb.offset = vec3(2.0f);
                     return !aabb.intersects_clip_space(view_proj);
                  });

            sort(begin(culled_blocks), end(culled_blocks), [this](const Block& a, const Block& b) -> bool {
                     vec4 a_pos = a.model;
                     vec4 b_pos = b.model;

                     auto a_trans = view_proj * a_pos;
                     auto b_trans = view_proj * b_pos;
                     float a_depth = a_trans.z + a_trans.w;
                     float b_depth = b_trans.z + b_trans.w;
                     return a_depth < b_depth;
                  });

            size_t active_blocks = culled_blocks.size();
            Log::log("Blocks: %zu.", active_blocks);

            for (size_t i = 0; i < active_blocks; i += Shader::MaxInstances)
            {
               size_t instances = std::min<size_t>(active_blocks, Shader::MaxInstances);
               Block *data;
               if (model.map(data))
               {
                  copy(begin(culled_blocks) + i, begin(culled_blocks) + i + instances, data);
                  model.unmap();
               }
               model.bind();
               glDrawElementsInstanced(GL_TRIANGLES, indices, GL_UNSIGNED_INT, nullptr, instances);
            }

            arrays.unbind();
            model.unbind();
            material.unbind();

            if (use_diffuse)
               tex.unbind(0);

            Sampler::unbind(0, Sampler::TrilinearClamp);
            shader->unbind();
         }
      };

      Drawable drawable;
      Shader shader;
      RenderQueue queue;
};

class BoxesApp : public LibretroGLApplication
{
   public:
      void get_system_info(retro_system_info& info) const override
      {
         info.library_name = "Boxes";
         info.library_version = "v1";
         info.valid_extensions = nullptr;
         info.need_fullpath = false;
         info.block_extract = false;
      }

      void get_system_av_info(retro_system_av_info& info) const override
      {
         info.timing.fps = 60.0;
         info.timing.sample_rate = 30000.0;
         info.geometry.base_width = 320;
         info.geometry.base_height = 180;
         info.geometry.max_width = 1920;
         info.geometry.max_height = 1080;
         info.geometry.aspect_ratio = 16.0f / 9.0f;
      }

      string get_application_name() const override
      {
         return "ModelView";
      }

      string get_application_name_short() const override
      {
         return "modelview";
      }

      vector<Resolution> get_resolutions() const override
      {
         vector<Resolution> res;
         res.push_back({320, 180});
         res.push_back({640, 360});
         res.push_back({1280, 720});
         res.push_back({1920, 1080});
         return res;
      }

      void update_global_data()
      {
         global.proj = perspective(45.0f, float(width) / float(height), 0.1f, 1000.0f);
         global.inv_proj = inverse(global.proj);
         global.view = lookAt(player_pos, player_pos + player_look_dir, vec3(0, 1, 0));
         global.view_nt = lookAt(vec3(0.0f), player_look_dir, vec3(0, 1, 0));
         global.inv_view = inverse(global.view);
         global.inv_view_nt = inverse(global.view_nt);

         global.vp = global.proj * global.view;
         global.inv_vp = inverse(global.vp);

         global.camera_pos = vec4(player_pos.x, player_pos.y, player_pos.z, 0.0);

         global_fragment.camera_pos = global.camera_pos;
         global_fragment.light_pos = vec4(50.0, 50.0, 0.0, 1.0);
         global_fragment.light_color = vec4(1.0);
         global_fragment.light_ambient = vec4(0.2);

         GlobalTransforms *buf;
         if (global_buffer.map(buf))
         {
            *buf = global;
            global_buffer.unmap();
         }

         GlobalFragmentData *frag_buf;
         if (global_fragment_buffer.map(frag_buf))
         {
            *frag_buf = global_fragment;
            global_fragment_buffer.unmap();
         }
      }

      void viewport_changed(const Resolution& res) override
      {
         width = res.width;
         height = res.height;

         update_global_data();
      }

      void update_input(float delta, const InputState::Analog& analog, const InputState::Buttons& buttons)
      {
         player_view_deg_y += analog.rx * -120.0f * delta;
         player_view_deg_x += analog.ry * -90.0f * delta;
         player_view_deg_x = clamp(player_view_deg_x, -80.0f, 80.0f);

         mat4 rotate_x = rotate(mat4(1.0), player_view_deg_x, vec3(1, 0, 0));
         mat4 rotate_y = rotate(mat4(1.0), player_view_deg_y, vec3(0, 1, 0));
         mat4 rotate_y_right = rotate(mat4(1.0), player_view_deg_y - 90.0f, vec3(0, 1, 0));

         player_look_dir = vec3(rotate_y * rotate_x * vec4(0, 0, -1, 1));
         vec3 right_walk_dir = vec3(rotate_y_right * vec4(0, 0, -1, 1));


         vec3 mod_speed = buttons.r ? vec3(240.0f) : vec3(120.0f);
         vec3 velocity = player_look_dir * vec3(analog.y * -0.25f) +
            right_walk_dir * vec3(analog.x * 0.25f);

         player_pos += velocity * mod_speed * delta;
         update_global_data();
      }

      void run(float delta, const InputState& input) override
      {
         auto analog = input.analog;
         if (fabsf(analog.x) < 0.3f)
            analog.x = 0.0f;
         if (fabsf(analog.y) < 0.3f)
            analog.y = 0.0f;
         if (fabsf(analog.rx) < 0.3f)
            analog.rx = 0.0f;
         if (fabsf(analog.ry) < 0.3f)
            analog.ry = 0.0f;
         update_input(delta, analog, input.pressed);

         glViewport(0, 0, width, height);
         glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
         glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

         glEnable(GL_CULL_FACE);
         glEnable(GL_DEPTH_TEST);
         glDepthFunc(GL_LEQUAL);

         global_buffer.bind();
         global_fragment_buffer.bind();

         scene.render(global.vp);

         skybox.tex.bind(0);
         Sampler::bind(0, Sampler::TrilinearClamp);
         skybox.shader.use();
         skybox.arrays.bind();
         glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
         skybox.arrays.unbind();
         skybox.shader.unbind();

         global_buffer.unbind();
         global_fragment_buffer.unbind();
         skybox.tex.unbind(0);
         Sampler::unbind(0, Sampler::TrilinearClamp);
      }

      void get_context_version(unsigned& major, unsigned& minor) const override
      {
         major = 3;
         minor = 3;
      }

      void load() override
      {
         global_buffer.init(GL_UNIFORM_BUFFER, sizeof(global), Buffer::WriteOnly, nullptr, Shader::GlobalVertexData);
         global_fragment_buffer.init(GL_UNIFORM_BUFFER,
               sizeof(global_fragment), Buffer::WriteOnly, nullptr, Shader::GlobalFragmentData);

         player_pos = vec3(0.0f);
         player_look_dir = vec3(0, 0, -1);
         player_view_deg_x = 0.0f;
         player_view_deg_y = 0.0f;

         scene.init();

         skybox.tex.load_texture_2d({Texture::TextureCube, {
                  "app/xpos.png",
                  "app/xneg.png",
                  "app/ypos.png",
                  "app/yneg.png",
                  "app/zpos.png",
                  "app/zneg.png",
               }, true});
         skybox.shader.init("app/shaders/skybox.vs", "app/shaders/skybox.fs");
         skybox.shader.set_samplers({{ "skybox", 0 }});
         skybox.shader.set_uniform_buffers({{ "ModelTransform", 2 }});
         vector<int8_t> vertices = { -1, -1, 1, -1, -1, 1, 1, 1 };
         skybox.vertex.init(GL_ARRAY_BUFFER, 8, Buffer::None, vertices.data());
         skybox.arrays.setup({{Shader::VertexLocation, 2, GL_BYTE, GL_FALSE, 0, 0}}, &skybox.vertex, nullptr);
      }

   private:
      unsigned width = 0;
      unsigned height = 0;

      float player_view_deg_x = 0.0f;
      float player_view_deg_y = 0.0f;
      vec3 player_pos;
      vec3 player_look_dir{0, 0, -1};

      struct GlobalTransforms
      {
         mat4 vp;
         mat4 view;
         mat4 view_nt;
         mat4 proj;
         mat4 inv_vp;
         mat4 inv_view;
         mat4 inv_view_nt;
         mat4 inv_proj;
         vec4 camera_pos;
      };

      struct GlobalFragmentData
      {
         vec4 camera_pos;
         vec4 light_pos;
         vec4 light_color;
         vec4 light_ambient;
      };

      GlobalTransforms global;
      GlobalFragmentData global_fragment;
      Buffer global_buffer;
      Buffer global_fragment_buffer;

      Scene scene;

      struct
      {
         Texture tex;
         Shader shader;
         VertexArray arrays;
         Buffer vertex;
      } skybox;
};

unique_ptr<LibretroGLApplication> libretro_gl_application_create()
{
   return Util::make_unique<BoxesApp>();
}

