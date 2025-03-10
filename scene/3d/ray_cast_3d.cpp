/*************************************************************************/
/*  ray_cast_3d.cpp                                                      */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "ray_cast_3d.h"

#include "collision_object_3d.h"
#include "core/config/engine.h"
#include "mesh_instance_3d.h"
#include "servers/physics_server_3d.h"

void RayCast3D::set_target_position(const Vector3 &p_point) {
	target_position = p_point;
	update_gizmos();

	if (Engine::get_singleton()->is_editor_hint()) {
		if (is_inside_tree()) {
			_update_debug_shape_vertices();
		}
	} else if (debug_shape) {
		_update_debug_shape();
	}
}

Vector3 RayCast3D::get_target_position() const {
	return target_position;
}

void RayCast3D::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
}

uint32_t RayCast3D::get_collision_mask() const {
	return collision_mask;
}

void RayCast3D::set_collision_mask_bit(int p_bit, bool p_value) {
	ERR_FAIL_INDEX_MSG(p_bit, 32, "Collision mask bit must be between 0 and 31 inclusive.");
	uint32_t mask = get_collision_mask();
	if (p_value) {
		mask |= 1 << p_bit;
	} else {
		mask &= ~(1 << p_bit);
	}
	set_collision_mask(mask);
}

bool RayCast3D::get_collision_mask_bit(int p_bit) const {
	ERR_FAIL_INDEX_V_MSG(p_bit, 32, false, "Collision mask bit must be between 0 and 31 inclusive.");
	return get_collision_mask() & (1 << p_bit);
}

bool RayCast3D::is_colliding() const {
	return collided;
}

Object *RayCast3D::get_collider() const {
	if (against.is_null()) {
		return nullptr;
	}

	return ObjectDB::get_instance(against);
}

int RayCast3D::get_collider_shape() const {
	return against_shape;
}

Vector3 RayCast3D::get_collision_point() const {
	return collision_point;
}

Vector3 RayCast3D::get_collision_normal() const {
	return collision_normal;
}

void RayCast3D::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	update_gizmos();

	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		set_physics_process_internal(p_enabled);
	}
	if (!p_enabled) {
		collided = false;
	}

	if (is_inside_tree() && get_tree()->is_debugging_collisions_hint()) {
		if (p_enabled) {
			_update_debug_shape();
		} else {
			_clear_debug_shape();
		}
	}
}

bool RayCast3D::is_enabled() const {
	return enabled;
}

void RayCast3D::set_exclude_parent_body(bool p_exclude_parent_body) {
	if (exclude_parent_body == p_exclude_parent_body) {
		return;
	}

	exclude_parent_body = p_exclude_parent_body;

	if (!is_inside_tree()) {
		return;
	}

	if (Object::cast_to<CollisionObject3D>(get_parent())) {
		if (exclude_parent_body) {
			exclude.insert(Object::cast_to<CollisionObject3D>(get_parent())->get_rid());
		} else {
			exclude.erase(Object::cast_to<CollisionObject3D>(get_parent())->get_rid());
		}
	}
}

bool RayCast3D::get_exclude_parent_body() const {
	return exclude_parent_body;
}

void RayCast3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				_update_debug_shape_vertices();
			}
			if (enabled && !Engine::get_singleton()->is_editor_hint()) {
				set_physics_process_internal(true);
			} else {
				set_physics_process_internal(false);
			}

			if (get_tree()->is_debugging_collisions_hint()) {
				_update_debug_shape();
			}

			if (Object::cast_to<CollisionObject3D>(get_parent())) {
				if (exclude_parent_body) {
					exclude.insert(Object::cast_to<CollisionObject3D>(get_parent())->get_rid());
				} else {
					exclude.erase(Object::cast_to<CollisionObject3D>(get_parent())->get_rid());
				}
			}

		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (enabled) {
				set_physics_process_internal(false);
			}

			if (debug_shape) {
				_clear_debug_shape();
			}

		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (!enabled) {
				break;
			}

			bool prev_collision_state = collided;
			_update_raycast_state();
			if (prev_collision_state != collided && get_tree()->is_debugging_collisions_hint()) {
				_update_debug_shape_material(true);
			}

		} break;
	}
}

void RayCast3D::_update_raycast_state() {
	Ref<World3D> w3d = get_world_3d();
	ERR_FAIL_COND(w3d.is_null());

	PhysicsDirectSpaceState3D *dss = PhysicsServer3D::get_singleton()->space_get_direct_state(w3d->get_space());
	ERR_FAIL_COND(!dss);

	Transform3D gt = get_global_transform();

	Vector3 to = target_position;
	if (to == Vector3()) {
		to = Vector3(0, 0.01, 0);
	}

	PhysicsDirectSpaceState3D::RayResult rr;

	if (dss->intersect_ray(gt.get_origin(), gt.xform(to), rr, exclude, collision_mask, collide_with_bodies, collide_with_areas)) {
		collided = true;
		against = rr.collider_id;
		collision_point = rr.position;
		collision_normal = rr.normal;
		against_shape = rr.shape;
	} else {
		collided = false;
		against = ObjectID();
		against_shape = 0;
	}
}

void RayCast3D::force_raycast_update() {
	_update_raycast_state();
}

void RayCast3D::add_exception_rid(const RID &p_rid) {
	exclude.insert(p_rid);
}

void RayCast3D::add_exception(const Object *p_object) {
	ERR_FAIL_NULL(p_object);
	const CollisionObject3D *co = Object::cast_to<CollisionObject3D>(p_object);
	if (!co) {
		return;
	}
	add_exception_rid(co->get_rid());
}

void RayCast3D::remove_exception_rid(const RID &p_rid) {
	exclude.erase(p_rid);
}

void RayCast3D::remove_exception(const Object *p_object) {
	ERR_FAIL_NULL(p_object);
	const CollisionObject3D *co = Object::cast_to<CollisionObject3D>(p_object);
	if (!co) {
		return;
	}
	remove_exception_rid(co->get_rid());
}

void RayCast3D::clear_exceptions() {
	exclude.clear();
}

void RayCast3D::set_collide_with_areas(bool p_clip) {
	collide_with_areas = p_clip;
}

bool RayCast3D::is_collide_with_areas_enabled() const {
	return collide_with_areas;
}

void RayCast3D::set_collide_with_bodies(bool p_clip) {
	collide_with_bodies = p_clip;
}

bool RayCast3D::is_collide_with_bodies_enabled() const {
	return collide_with_bodies;
}

void RayCast3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &RayCast3D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &RayCast3D::is_enabled);

	ClassDB::bind_method(D_METHOD("set_target_position", "local_point"), &RayCast3D::set_target_position);
	ClassDB::bind_method(D_METHOD("get_target_position"), &RayCast3D::get_target_position);

	ClassDB::bind_method(D_METHOD("is_colliding"), &RayCast3D::is_colliding);
	ClassDB::bind_method(D_METHOD("force_raycast_update"), &RayCast3D::force_raycast_update);

	ClassDB::bind_method(D_METHOD("get_collider"), &RayCast3D::get_collider);
	ClassDB::bind_method(D_METHOD("get_collider_shape"), &RayCast3D::get_collider_shape);
	ClassDB::bind_method(D_METHOD("get_collision_point"), &RayCast3D::get_collision_point);
	ClassDB::bind_method(D_METHOD("get_collision_normal"), &RayCast3D::get_collision_normal);

	ClassDB::bind_method(D_METHOD("add_exception_rid", "rid"), &RayCast3D::add_exception_rid);
	ClassDB::bind_method(D_METHOD("add_exception", "node"), &RayCast3D::add_exception);

	ClassDB::bind_method(D_METHOD("remove_exception_rid", "rid"), &RayCast3D::remove_exception_rid);
	ClassDB::bind_method(D_METHOD("remove_exception", "node"), &RayCast3D::remove_exception);

	ClassDB::bind_method(D_METHOD("clear_exceptions"), &RayCast3D::clear_exceptions);

	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &RayCast3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &RayCast3D::get_collision_mask);

	ClassDB::bind_method(D_METHOD("set_collision_mask_bit", "bit", "value"), &RayCast3D::set_collision_mask_bit);
	ClassDB::bind_method(D_METHOD("get_collision_mask_bit", "bit"), &RayCast3D::get_collision_mask_bit);

	ClassDB::bind_method(D_METHOD("set_exclude_parent_body", "mask"), &RayCast3D::set_exclude_parent_body);
	ClassDB::bind_method(D_METHOD("get_exclude_parent_body"), &RayCast3D::get_exclude_parent_body);

	ClassDB::bind_method(D_METHOD("set_collide_with_areas", "enable"), &RayCast3D::set_collide_with_areas);
	ClassDB::bind_method(D_METHOD("is_collide_with_areas_enabled"), &RayCast3D::is_collide_with_areas_enabled);

	ClassDB::bind_method(D_METHOD("set_collide_with_bodies", "enable"), &RayCast3D::set_collide_with_bodies);
	ClassDB::bind_method(D_METHOD("is_collide_with_bodies_enabled"), &RayCast3D::is_collide_with_bodies_enabled);

	ClassDB::bind_method(D_METHOD("set_debug_shape_custom_color", "debug_shape_custom_color"), &RayCast3D::set_debug_shape_custom_color);
	ClassDB::bind_method(D_METHOD("get_debug_shape_custom_color"), &RayCast3D::get_debug_shape_custom_color);

	ClassDB::bind_method(D_METHOD("set_debug_shape_thickness", "debug_shape_thickness"), &RayCast3D::set_debug_shape_thickness);
	ClassDB::bind_method(D_METHOD("get_debug_shape_thickness"), &RayCast3D::get_debug_shape_thickness);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "exclude_parent"), "set_exclude_parent_body", "get_exclude_parent_body");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "target_position"), "set_target_position", "get_target_position");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");

	ADD_GROUP("Collide With", "collide_with");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collide_with_areas", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collide_with_areas", "is_collide_with_areas_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collide_with_bodies", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collide_with_bodies", "is_collide_with_bodies_enabled");

	ADD_GROUP("Debug Shape", "debug_shape");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "debug_shape_custom_color"), "set_debug_shape_custom_color", "get_debug_shape_custom_color");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_shape_thickness", PROPERTY_HINT_RANGE, "1,5"), "set_debug_shape_thickness", "get_debug_shape_thickness");
}

float RayCast3D::get_debug_shape_thickness() const {
	return debug_shape_thickness;
}

void RayCast3D::_update_debug_shape_vertices() {
	debug_shape_vertices.clear();
	debug_line_vertices.clear();

	if (target_position == Vector3()) {
		return;
	}

	debug_line_vertices.push_back(Vector3());
	debug_line_vertices.push_back(target_position);

	if (debug_shape_thickness > 1) {
		float scale_factor = 100.0;
		Vector3 dir = Vector3(target_position).normalized();
		// Draw truncated pyramid
		Vector3 normal = (fabs(dir.x) + fabs(dir.y) > CMP_EPSILON) ? Vector3(-dir.y, dir.x, 0).normalized() : Vector3(0, -dir.z, dir.y).normalized();
		normal *= debug_shape_thickness / scale_factor;
		int vertices_strip_order[14] = { 4, 5, 0, 1, 2, 5, 6, 4, 7, 0, 3, 2, 7, 6 };
		for (int v = 0; v < 14; v++) {
			Vector3 vertex = vertices_strip_order[v] < 4 ? normal : normal / 3.0 + target_position;
			debug_shape_vertices.push_back(vertex.rotated(dir, Math_PI * (0.5 * (vertices_strip_order[v] % 4) + 0.25)));
		}
	}
}

void RayCast3D::set_debug_shape_thickness(const float p_debug_shape_thickness) {
	debug_shape_thickness = p_debug_shape_thickness;
	update_gizmos();

	if (Engine::get_singleton()->is_editor_hint()) {
		if (is_inside_tree()) {
			_update_debug_shape_vertices();
		}
	} else if (debug_shape) {
		_update_debug_shape();
	}
}

const Vector<Vector3> &RayCast3D::get_debug_shape_vertices() const {
	return debug_shape_vertices;
}

const Vector<Vector3> &RayCast3D::get_debug_line_vertices() const {
	return debug_line_vertices;
}

void RayCast3D::set_debug_shape_custom_color(const Color &p_color) {
	debug_shape_custom_color = p_color;
	if (debug_material.is_valid()) {
		_update_debug_shape_material();
	}
}

Ref<StandardMaterial3D> RayCast3D::get_debug_material() {
	_update_debug_shape_material();
	return debug_material;
}

const Color &RayCast3D::get_debug_shape_custom_color() const {
	return debug_shape_custom_color;
}

void RayCast3D::_create_debug_shape() {
	_update_debug_shape_material();

	Ref<ArrayMesh> mesh = memnew(ArrayMesh);

	MeshInstance3D *mi = memnew(MeshInstance3D);
	mi->set_mesh(mesh);

	add_child(mi);
	debug_shape = mi;
}

void RayCast3D::_update_debug_shape_material(bool p_check_collision) {
	if (!debug_material.is_valid()) {
		Ref<StandardMaterial3D> material = memnew(StandardMaterial3D);
		debug_material = material;

		material->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
		// Use double-sided rendering so that the RayCast can be seen if the camera is inside.
		material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	}

	Color color = debug_shape_custom_color;
	if (color == Color(0.0, 0.0, 0.0)) {
		// Use the default debug shape color defined in the Project Settings.
		color = get_tree()->get_debug_collisions_color();
	}

	if (p_check_collision && collided) {
		if ((color.get_h() < 0.055 || color.get_h() > 0.945) && color.get_s() > 0.5 && color.get_v() > 0.5) {
			// If base color is already quite reddish, highlight collision with green color
			color = Color(0.0, 1.0, 0.0, color.a);
		} else {
			// Else, highlight collision with red color
			color = Color(1.0, 0, 0, color.a);
		}
	}

	Ref<StandardMaterial3D> material = static_cast<Ref<StandardMaterial3D>>(debug_material);
	material->set_albedo(color);
}

void RayCast3D::_update_debug_shape() {
	if (!enabled) {
		return;
	}

	if (!debug_shape) {
		_create_debug_shape();
	}

	MeshInstance3D *mi = static_cast<MeshInstance3D *>(debug_shape);
	Ref<ArrayMesh> mesh = mi->get_mesh();
	if (!mesh.is_valid()) {
		return;
	}

	_update_debug_shape_vertices();

	mesh->clear_surfaces();

	Array a;
	a.resize(Mesh::ARRAY_MAX);

	uint32_t flags = 0;
	int surface_count = 0;

	if (!debug_line_vertices.is_empty()) {
		a[Mesh::ARRAY_VERTEX] = debug_line_vertices;
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_LINES, a, Array(), Dictionary(), flags);
		mesh->surface_set_material(surface_count, debug_material);
		++surface_count;
	}

	if (!debug_shape_vertices.is_empty()) {
		a[Mesh::ARRAY_VERTEX] = debug_shape_vertices;
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLE_STRIP, a, Array(), Dictionary(), flags);
		mesh->surface_set_material(surface_count, debug_material);
		++surface_count;
	}
}

void RayCast3D::_clear_debug_shape() {
	if (!debug_shape) {
		return;
	}

	MeshInstance3D *mi = static_cast<MeshInstance3D *>(debug_shape);
	if (mi->is_inside_tree()) {
		mi->queue_delete();
	} else {
		memdelete(mi);
	}

	debug_shape = nullptr;
}

RayCast3D::RayCast3D() {
}
