// vim: set filetype=cpp foldmethod=marker foldmarker={,} :
#include "firmware.h"

#if 0
#define movedebug(...) debug(__VA_ARGS__)
#else
#define movedebug(...) do {} while (0)
#endif

//#define TIMING

// Loop function handles all regular updates.

#ifdef HAVE_TEMPS
static void handle_temps(uint32_t current_time, uint32_t longtime) {
	if (next_temp_time > 0)
		return;
	if (adc_phase == 0) {
		next_temp_time = ~0;
		return;
	}
	if (adc_phase == 1) {
		if (requested_temp < num_temps)
			temp_current = requested_temp;
		else {
			// Find the temp to measure next time.
			uint8_t i;
			for (i = 1; i <= num_temps; ++i) {
				uint8_t next = (temp_current + i) % num_temps;
				if (((temps[next].adctarget < MAXINT && temps[next].adctarget >= 0)
						 || (temps[next].adcmin_alarm >= 0 && temps[next].adcmin_alarm < MAXINT)
						 || temps[next].adcmax_alarm < MAXINT
#ifdef HAVE_GPIOS
						 || temps[next].following_gpios < num_gpios
#endif
						 )
						&& temps[next].thermistor_pin.valid()) {
					temp_current = next;
					break;
				}
			}
			// If there is no temperature handling to do; disable (and abort the current one as well; it is no longer needed).
			if (i > num_temps) {
				adc_phase = 0;
				return;
			}
		}
		adc_start(temps[temp_current].thermistor_pin.pin);
		adc_phase = 2;
		return;
	}
	int32_t temp = temps[temp_current].get_value();
	if (temp < 0) {	// Not done yet.
		return;
	}
	next_temp_time = 100000;
	//debug("done temperature %d %d", temp_current, temp);
	if (requested_temp == temp_current) {
		//debug("replying temp");
		requested_temp = ~0;
		ReadFloat f;
		f.f = temps[temp_current].fromadc(temp);
		//debug("read temp %f", F(f.f));
		reply[0] = 2 + sizeof(float);
		reply[1] = CMD_TEMP;
		for (uint8_t b = 0; b < sizeof(float); ++b)
			reply[2 + b] = f.b[b];
		reply_ready = true;
		try_send_next();
	}
	//debug("temp for %d: %d", temp_current, temp);
	// Set the phase so next time another temp is measured.
	adc_phase = 1;
	// First of all, if an alarm should be triggered, do so.  Adc values are higher for lower temperatures.
	//debug("alarms: %d %d %d %d", temp_current, temps[temp_current].adcmin_alarm, temps[temp_current].adcmax_alarm, temp);
	if ((temps[temp_current].adcmin_alarm < MAXINT && temps[temp_current].adcmin_alarm >= temp) || temps[temp_current].adcmax_alarm <= temp) {
		temps[temp_current].min_alarm = NAN;
		temps[temp_current].max_alarm = NAN;
		temps[temp_current].adcmin_alarm = MAXINT;
		temps[temp_current].adcmax_alarm = MAXINT;
		temps[temp_current].alarm = true;
		try_send_next();
	}
#ifdef HAVE_GPIOS
	// And handle any linked gpios.
	for (uint8_t g = temps[temp_current].following_gpios; g < num_gpios; g = gpios[g].next) {
		//debug("setting gpio for temp %d: %d %d", temp_current, temp, g->adcvalue);
		// adc values are lower for higher temperatures.
		if (temp < gpios[g].adcvalue)
			SET(gpios[g].pin);
		else
			RESET(gpios[g].pin);
	}
#endif
	// If we don't have model settings, simply use the target as a switch between on and off.
	/* Don't use those values yet.
	if (true || temps[temp_current].core_C <= 0 || temps[temp_current].shell_C <= 0 || temps[temp_current].transfer <= 0 || temps[temp_current].radiation <= 0)
	*/
	{
		// No valid settings; use simple on/off-regime based on current temperature only.  Note that adc values are lower for higher temperatures.
		if (temp > temps[temp_current].adctarget) {
			if (!temps[temp_current].is_on) {
				//debug("switching on %d", temp_current);
				SET(temps[temp_current].power_pin);
				temps[temp_current].is_on = true;
				temps[temp_current].last_temp_time = current_time;
				++temps_busy;
			}
			else
				temps[temp_current].time_on += current_time - temps[temp_current].last_temp_time;
		}
		else {
			if (temps[temp_current].is_on) {
				//debug("switching off %d", temp_current);
				RESET(temps[temp_current].power_pin);
				temps[temp_current].is_on = false;
				temps[temp_current].time_on += current_time - temps[temp_current].last_temp_time;
				--temps_busy;
			}
		}
		return;
	}
	/*
	// TODO: Make this work and decide on units.
	// We have model settings.
	uint32_t dt = current_time - temps[temp_current].last_temp_time;
	if (dt == 0)
		return;
	temps[temp_current].last_temp_time = current_time;
	// Heater and core/shell transfer.
	if (temps[temp_current].is_on)
		temps[temp_current].core_T += temps[temp_current].power / temps[temp_current].core_C * dt;
	float Q = temps[temp_current].transfer * (temps[temp_current].core_T - temps[temp_current].shell_T) * dt;
	temps[temp_current].core_T -= Q / temps[temp_current].core_C;
	temps[temp_current].shell_T += Q / temps[temp_current].shell_C;
	if (temps[temp_current].is_on)
		temps[temp_current].core_T += temps[temp_current].power / temps[temp_current].core_C * dt / 2;
	// Set shell to measured value.
	temps[temp_current].shell_T = temp;
	// Add energy if required.
	float E = temps[temp_current].core_T * temps[temp_current].core_C + temps[temp_current].shell_T * temps[temp_current].shell_C;
	float T = E / (temps[temp_current].core_C + temps[temp_current].shell_C);
	// Set the pin to correct value.
	if (T < temps[temp_current].target) {
		if (!temps[temp_current].is_on) {
			SET(temps[temp_current].power_pin);
			temps[temp_current].is_on = true;
			++temps_busy;
		}
		else
			temps[temp_current].time_on += current_time - temps[temp_current].last_temp_time;
	}
	else {
		if (temps[temp_current].is_on) {
			RESET(temps[temp_current].power_pin);
			temps[temp_current].is_on = false;
			temps[temp_current].time_on += current_time - temps[temp_current].last_temp_time;
			--temps_busy;
		}
	}
	*/
}
#endif

#ifdef HAVE_SPACES
static bool steps_limited;
static float nums = 0, avgs = 0, currents = 0;
static void check_distance(Motor *mtr, float distance, float dt, float &factor) {
	if (isnan(distance) || distance == 0) {
		mtr->target_dist = 0;
		return;
	}
	//debug("cd %f %f", F(distance), F(dt));
	mtr->target_dist = distance;
	mtr->target_v = distance / dt;
	float v = fabs(distance / dt);
	int8_t s = (mtr->target_v < 0 ? -1 : 1);
	// When turning around, ignore limits (they shouldn't have been violated anyway).
	if (mtr->last_v * s < 0) {
		//debug("!");
		mtr->last_v = 0;
	}
	// Limit v.
	if (v > mtr->limit_v) {
		//movedebug("v %f %f", F(v), F(mtr->limit_v));
		distance = (s * mtr->limit_v) * dt;
		v = fabs(distance / dt);
	}
	//debug("cd2 %f %f", F(distance), F(dt));
	// Limit a+.
	float limit_dv = mtr->limit_a * dt;
	if (v - mtr->last_v * s > limit_dv) {
		//movedebug("a+ %f %f %f %d", F(mtr->target_v), F(limit_dv), F(mtr->last_v), s);
		distance = (limit_dv * s + mtr->last_v) * dt;
		v = fabs(distance / dt);
	}
	//debug("cd3 %f %f", F(distance), F(dt));
	// Limit a-.
	float max_dist = (mtr->endpos - mtr->current_pos) * s;
	if (max_dist > 0 && v * v / 2 / mtr->limit_a > max_dist) {
		//movedebug("a- %f %f %f %f %d", F(mtr->endpos), F(mtr->limit_a), F(max_dist), F(mtr->current_pos), s);
		v = sqrt(max_dist * 2 * mtr->limit_a);
		distance = s * v * dt;
	}
	//debug("cd4 %f %f", F(distance), F(dt));
	int32_t steps;
	if (!isnan(mtr->current_pos))
		steps = int32_t((mtr->current_pos + distance) * mtr->steps_per_m + .5) - int32_t(mtr->current_pos * mtr->steps_per_m + .5);
	else
		steps = int32_t(distance * mtr->steps_per_m + .5);
	// Limit steps per iteration.
	if (abs(steps) > mtr->max_steps) {
		//debug("s %f %f %f %ld %f", F(distance), F(mtr->current_pos), F(mtr->steps_per_m), F(steps), F(dt));
		distance = s * (mtr->max_steps + .1) / mtr->steps_per_m;
		steps_limited = true;
	}
	//debug("cd5 %f %f", F(distance), F(dt));
	float f = distance / mtr->target_dist;
	//movedebug("checked %f %f", F(mtr->target_dist), F(distance));
	if (f < factor)
		factor = f;
}

static void move_axes(Space *s, uint32_t current_time, float &factor) {
	float motors_target[s->num_motors];
	bool ok = true;
	space_types[s->type].xyz2motors(s, motors_target, &ok);
	// Try again if it didn't work; it should have moved target to a better location.
	if (!ok)
		space_types[s->type].xyz2motors(s, motors_target, &ok);
	//movedebug("ok %d", ok);
	for (uint8_t m = 0; m < s->num_motors; ++m) {
		//movedebug("move %d %f %f %f", m, F(target[m]), F(motors_target[m]), F(s->motor[m]->current_pos));
		check_distance(s->motor[m], motors_target[m] - s->motor[m]->current_pos, (current_time - last_time) / 1e6, factor);
	}
}

static bool do_steps(float &factor, uint32_t current_time) {
	if (factor <= 0) {
		next_motor_time = 0;
		return true;
	}
	// Find out if there is any movement at all; if not, set factor to 0 so there will eventually be a step that is large enough for movement.
	int32_t max_steps = 0;
	float f_correction = 1;
	float factor1 = INFINITY;
	// See if any motor does any steps; if not: wait.
	for (uint8_t s = 0; s < num_spaces; ++s) {
		Space &sp = spaces[s];
		if (!sp.active)
			continue;
		for (uint8_t m = 0; m < sp.num_motors; ++m) {
			Motor &mtr = *sp.motor[m];
			float targetsteps(fabs(mtr.target_dist) * mtr.steps_per_m + .5);
			float myfactor = .5 / targetsteps;
			if (myfactor < factor1)
				factor1 = factor;
			if (int32_t(targetsteps) < 1) {
				//debug("steps %d %d %f %f", s, m, targetsteps, mtr.target_dist);
				mtr.steps = 0;
				continue;
			}
			if (!isnan(mtr.current_pos)) {
				float target = mtr.current_pos + mtr.target_dist * factor;
				mtr.steps = int32_t(target * mtr.steps_per_m + .5) - int32_t(mtr.current_pos * mtr.steps_per_m + .5);
			}
			else
				mtr.steps = int32_t(mtr.target_dist * factor * mtr.steps_per_m + .5);
			if (abs(mtr.steps) > max_steps)
				max_steps = abs(mtr.steps);
			if (factor < 1) {
				if ((fabs(mtr.steps) + 1.0) / int32_t(targetsteps) < f_correction)
					f_correction = (fabs(mtr.steps) + 1.0) / int32_t(targetsteps);
			}
		}
	}
	//movedebug("do steps %f %d", F(factor), max_steps);
	if (max_steps == 0) {
		currents += 1;
		if (!isinf(factor1)) {
			next_motor_time = (current_time - last_time) * factor1;
			//debug("setting next motor time to %ld", next_motor_time);
			next_motor_time = 0;
		}
		if (factor < 1)
			factor = 0;
		//movedebug("do steps0 %f", F(factor));
		return true;
	}
	avgs += currents;
	nums += 1;
	currents = 0;
	next_motor_time = 0;
	// Adjust start time if factor < 1.
	if (f_correction > 0 && f_correction < 1) {
		start_time += (current_time - last_time) * ((1 - f_correction) * .99);
		movedebug("correct: %f %d", F(f_correction), int(start_time));
	}
	else
		movedebug("no correct: %f %d", F(f_correction), int(start_time));
	last_time = current_time;
	// Move the motors.
	for (uint8_t s = 0; s < num_spaces; ++s) {
		Space &sp = spaces[s];
		if (!sp.active)
			continue;
		for (uint8_t m = 0; m < sp.num_motors; ++m) {
			Motor &mtr = *sp.motor[m];
			// Check limit switches.
			float target = mtr.current_pos + mtr.target_dist * factor;
			if (mtr.steps == 0) {
				// Allow increase of last_v so limit_a doesn't block all movement.
				if (mtr.last_v < mtr.target_v * factor)
					mtr.last_v = mtr.target_v * factor;
				mtr.current_pos = target;
				continue;
			}
			if (mtr.steps > 0 ? GET(mtr.limit_max_pin, false) : GET(mtr.limit_min_pin, false)) {
				// Hit endstop; abort current move and notify host.
				debug("hit limit %d %d %d %ld", s, m, mtr.target_dist > 0, F(long(mtr.steps)));
				mtr.last_v = 0;
				mtr.limits_pos = isnan(mtr.current_pos) ? INFINITY * (mtr.target_dist > 0 ? 1 : -1) : mtr.current_pos;
				if (moving && cbs_after_current_move > 0) {
					num_movecbs += cbs_after_current_move;
					cbs_after_current_move = 0;
				}
				//debug("aborting for limit");
				abort_move();
				num_movecbs += next_move();
				try_send_next();
				return false;
			}
			if (!steps_limited)
				mtr.last_v = mtr.target_v * factor;
			//debug("%f %f %d", F(mtr.current_pos), F(target), mtr.steps);
			mtr.current_pos = target;
			// Set direction pin.
			if (mtr.steps > 0)
				SET(mtr.dir_pin);
			else
				RESET(mtr.dir_pin);
			microdelay();
			// Move.
			for (int16_t st = 0; st < abs(mtr.steps); ++st) {
				SET(mtr.step_pin);
				RESET(mtr.step_pin);
			}
		}
	}
	for (uint8_t s = 0; s < num_spaces; ++s) {
		Space &sp = spaces[s];
		if (!sp.active)
			continue;
		for (uint8_t a = 0; a < sp.num_axes; ++a)
			sp.axis[a]->current += (sp.axis[a]->target - sp.axis[a]->current) * factor;
	}
	return true;
}


static void handle_motors(uint32_t current_time, uint32_t longtime) {
	if (next_motor_time > 0)
		return;
	// Check sense pins.
	for (uint8_t s = 0; s < num_spaces; ++s) {
		Space &sp = spaces[s];
		for (uint8_t m = 0; m < sp.num_motors; ++m) {
			if (!sp.motor[m]->sense_pin.valid())
				continue;
			if (!isnan(sp.motor[m]->current_pos) && GET(sp.motor[m]->sense_pin, false) ^ bool(sp.motor[m]->sense_state & 0x80)) {
				sp.motor[m]->sense_state ^= 0x80;
				sp.motor[m]->sense_state |= 1;
				sp.motor[m]->sense_pos = sp.motor[m]->current_pos;
				try_send_next();
			}
		}
	}
	// Check for move.
	if (!moving) {
		next_motor_time = ~0;
		//debug("setting next motor time to ~0");
		return;
	}
	last_active = longtime;
	steps_limited = false;
	float factor = 1;
	float t = (current_time - start_time) / 1e6;
	//buffered_debug("f%f %f %f %ld %ld", F(t), F(t0), F(tp), F(long(current_time)), F(long(start_time)));
	if (t >= t0 + tp) {	// Finish this move and prepare next.
		movedebug("finishing %f %f %f %ld %ld", F(t), F(t0), F(tp), F(long(current_time)), F(long(start_time)));
		//buffered_debug("a");
		for (uint8_t s = 0; s < num_spaces; ++s) {
			Space &sp = spaces[s];
			if (!sp.active)
				continue;
			for (uint8_t a = 0; a < sp.num_axes; ++a) {
				//debug("before source %d %f %f", a, F(axis[a].source), F(axis[a].motor.dist));
				if (!isnan(sp.axis[a]->dist)) {
					sp.axis[a]->source += sp.axis[a]->dist;
					sp.axis[a]->dist = NAN;
					// Set this here, so it isn't set if dist was NaN to begin with.
					// Note that target is not set for future iterations, but it isn't changed.
					sp.axis[a]->target = sp.axis[a]->source;
				}
				//debug("after source %d %f %f %f %f", a, F(sp.axis[a]->source), F(sp.axis[a]->dist), F(sp.motor[a]->current_pos), F(factor));
			}
			move_axes(&sp, current_time, factor);
			//debug("f %f", F(factor));
		}
		//debug("f2 %f %ld %ld", F(factor), F(last_time), F(current_time));
		if (!do_steps(factor, current_time))
			return;
		//debug("f3 %f", F(factor));
		// Start time may have changed; recalculate t.
		t = (current_time - start_time) / 1e6;
		if (t / (t0 + tp) >= done_factor) {
			//buffered_debug("b");
			moving = false;
			uint8_t had_cbs = cbs_after_current_move;
			cbs_after_current_move = 0;
			had_cbs += next_move();
			if (moving) {
				//buffered_debug("c");
				//debug("movecb 1");
				num_movecbs += had_cbs;
				try_send_next();
				return;
			}
			//buffered_debug("d");
			cbs_after_current_move += had_cbs;
			if (factor == 1) {
				//buffered_debug("e");
				moving = false;
				//debug("movecb 1");
				num_movecbs += cbs_after_current_move;
				cbs_after_current_move = 0;
				try_send_next();
				//debug("done %f", F(avgs / nums));
				avgs = 0;
				nums = 0;
			}
			else {
				moving = true;
				next_motor_time = 0;
				//if (factor > 0)
				//	debug("not done %f", F(factor));
			}
		}
		return;
	}
	if (t < t0) {	// Main part.
		float t_fraction = t / t0;
		float current_f = (f1 * (2 - t_fraction) + f2 * t_fraction) * t_fraction;
		movedebug("main t %f t0 %f tp %f tfrac %f f1 %f f2 %f cf %f", F(t), F(t0), F(tp), F(t_fraction), F(f1), F(f2), F(current_f));
		for (uint8_t s = 0; s < num_spaces; ++s) {
			Space &sp = spaces[s];
			//movedebug("try %d %d", s, sp.active);
			if (!sp.active)
				continue;
			for (uint8_t a = 0; a < sp.num_axes; ++a) {
				if (isnan(sp.axis[a]->dist) || sp.axis[a]->dist == 0) {
					sp.axis[a]->target = NAN;
					continue;
				}
				sp.axis[a]->target = sp.axis[a]->source + sp.axis[a]->dist * current_f;
				//movedebug("do %d %d %f %f", s, a, F(sp.axis[a]->dist), F(target[a]));
			}
			move_axes(&sp, current_time, factor);
		}
	}
	else {	// Connector part.
		movedebug("connector %f %f %f", F(t), F(t0), F(tp));
		float tc = t - t0;
		for (uint8_t s = 0; s < num_spaces; ++s) {
			Space &sp = spaces[s];
			if (!sp.active)
				continue;
			for (uint8_t a = 0; a < sp.num_axes; ++a) {
				if ((isnan(sp.axis[a]->dist) || sp.axis[a]->dist == 0) && (isnan(sp.axis[a]->next_dist) || sp.axis[a]->next_dist == 0)) {
					sp.axis[a]->target = NAN;
					continue;
				}
				float t_fraction = tc / tp;
				float current_f2 = fp * (2 - t_fraction) * t_fraction;
				float current_f3 = fq * t_fraction * t_fraction;
				sp.axis[a]->target = sp.axis[a]->source + sp.axis[a]->main_dist + sp.axis[a]->dist * current_f2 + sp.axis[a]->next_dist * current_f3;
			}
			move_axes(&sp, current_time, factor);
		}
	}
	do_steps(factor, current_time);
}
#endif

#ifdef HAVE_AUDIO
static void handle_audio(uint32_t current_time, uint32_t longtime) {
	if (audio_head != audio_tail) {
		last_active = longtime;
		int32_t sample = (current_time - audio_start) / audio_us_per_sample;
		int32_t audio_byte = sample >> 3;
		while (audio_byte >= AUDIO_FRAGMENT_SIZE) {
			//debug("next audio fragment");
			if ((audio_tail + 1) % AUDIO_FRAGMENTS == audio_head)
			{
				//debug("continue audio");
				continue_cb |= 2;
				try_send_next();
			}
			audio_head = (audio_head + 1) % AUDIO_FRAGMENTS;
			if (audio_tail == audio_head) {
				//debug("audio done");
				next_audio_time = ~0;
				return;
			}
			audio_byte -= AUDIO_FRAGMENT_SIZE;
			// us per fragment = us/sample*sample/fragment
			audio_start += audio_us_per_sample * 8 * AUDIO_FRAGMENT_SIZE;
		}
		uint8_t old_state = audio_state;
		audio_state = (audio_buffer[audio_head][audio_byte] >> (sample & 7)) & 1;
		if (audio_state != old_state) {
			for (uint8_t s = 0; s < num_spaces; ++s) {
				Space &sp = spaces[s];
				for (uint8_t m = 0; m < sp.num_motors; ++m) {
					if (!(sp.motor[m]->audio_flags & Motor::PLAYING))
						continue;
					if (audio_state > old_state)
						SET(sp.motor[m]->dir_pin);
					else
						RESET(sp.motor[m]->dir_pin);
					microdelay();
					SET(sp.motor[m]->step_pin);
					RESET(sp.motor[m]->step_pin);
				}
			}
		}
	}
}
#endif

static void handle_led(uint32_t current_time) {
	uint32_t timing = temps_busy > 0 ? 1000000 / 100 : 1000000 / 50;
	while (current_time - led_last >= timing) {
		led_last += timing;
		led_phase += 1;
	}
	next_led_time = timing - (current_time - led_last);
	//debug("t %ld", F(next_led_time));
	led_phase %= 50;
	// Timings read from https://en.wikipedia.org/wiki/File:Wiggers_Diagram.png (phonocardiogram).
	bool state = (led_phase <= 4 || (led_phase >= 14 && led_phase <= 17));
	if (state)
		SET(led_pin);
	else
		RESET(led_pin);
}

void loop() {
	// Handle any arch-specific periodic things.
	arch_run();
	// Timekeeping.
	//debug("last current time %ld", F(long(last_current_time)));
	uint32_t current_time;
	uint32_t longtime;
	get_current_times(&current_time, &longtime);
	uint32_t delta = current_time - last_current_time;
	//debug("delta: %ld", F(long(delta)));
	if (!Serial.available()) {
		uint32_t next_time = next_led_time;
#ifdef HAVE_TEMPS
		if (next_temp_time < next_time)
			next_time = next_temp_time;
#endif
#ifdef HAVE_SPACES
		if (next_motor_time < next_time)
			next_time = next_motor_time;
#endif
#ifdef HAVE_AUDIO
		if (next_audio_time < next_time)
			next_time = next_audio_time;
#endif
		if (next_time > delta + 10000) {
			// Wait for next event; compensate for time already during this iteration (+10ms, to be sure); don't alter "infitity" flag.
			wait_for_event(~next_time ? next_time - delta - 10000: ~0, last_current_time);
			get_current_times(&current_time, &longtime);
			delta = current_time - last_current_time;
		}
	}
	last_current_time = current_time;
	// Update next_*_time.
	if (~next_led_time)
		next_led_time = next_led_time > delta ? next_led_time - delta : 0;
#ifdef HAVE_SPACES
	if (~next_motor_time)
		next_motor_time = next_motor_time > delta ? next_motor_time - delta : 0;
#endif
#ifdef HAVE_TEMPS
	if (~next_temp_time)
		next_temp_time = next_temp_time > delta ? next_temp_time - delta : 0;
#endif
#ifdef HAVE_AUDIO
	if (~next_audio_time)
		next_audio_time = next_audio_time > delta ? next_audio_time - delta : 0;
#endif
	// Timeouts.  Do this before calling other things, because last_active may be updated and become larger than longtime.
#ifdef HAVE_SPACES
	if (motors_busy && (longtime - last_active) / 1e3 > motor_limit) {
		debug("motor timeout %ld %ld %f", F(long(longtime)), F(long(last_active)), F(motor_limit));
		for (uint8_t s = 0; s < num_spaces; ++s) {
			Space &sp = spaces[s];
			for (uint8_t m = 0; m < sp.num_motors; ++m) {
				RESET(sp.motor[m]->enable_pin);
				sp.motor[m]->current_pos = NAN;
			}
			for (uint8_t a = 0; a < sp.num_axes; ++a) {
				sp.axis[a]->current = NAN;
				sp.axis[a]->source = NAN;
			}
		}
		motors_busy = false;
		which_autosleep |= 1;
	}
#endif
#ifdef HAVE_TEMPS
	if (temps_busy > 0 && (longtime - last_active) / 1e3 > temp_limit) {
		for (uint8_t current_t = 0; current_t < num_temps; ++current_t) {
			RESET(temps[current_t].power_pin);
			temps[current_t].target = NAN;
			temps[current_t].adctarget = MAXINT;
			temps[current_t].is_on = false;
		}
		temps_busy = 0;
		which_autosleep |= 2;
	}
#endif
	if (which_autosleep != 0)
		try_send_next();
	// Handle all periodic things.
	if (!next_led_time)
		handle_led(current_time);	// heart beat.
#ifdef TIMING
	uint32_t first_t = utime();
#endif
#ifdef HAVE_TEMPS
	if (!next_temp_time)
		handle_temps(current_time, longtime);	// Periodic temps stuff: temperature regulation.
#endif
#ifdef TIMING
	uint32_t temp_t = utime() - current_time;
#endif
#ifdef HAVE_SPACES
	if (!next_motor_time)
		handle_motors(current_time, longtime);	// Movement.
#endif
#ifdef TIMING
	uint32_t motor_t = utime() - current_time;
#endif
#ifdef TIMING
	uint32_t led_t = utime() - current_time;
#endif
#ifdef HAVE_AUDIO
	if (!next_audio_time)
		handle_audio(current_time, longtime);
#endif
#ifdef TIMING
	uint32_t audio_t = utime() - current_time;
#endif
	serial();
#ifdef TIMING
	uint32_t serial_t = utime() - first_t;
#endif
#ifdef TIMING
	uint32_t end_t = utime() - current_time;
	end_t -= audio_t;
	audio_t -= led_t;
	led_t -= motor_t;
	motor_t -= temp_t;
	static int waiter = 0;
	if (waiter > 0)
		waiter -= 1;
	else {
		waiter = 977;
		debug("t: serial %ld temp %ld motor %ld led %ld audio %ld end %ld", F(serial_t), F(temp_t), F(motor_t), F(led_t), F(audio_t), F(end_t));
	}
#endif
}
