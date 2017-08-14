/*
 * This is an implementation of the SSA construction algorithm:
 *	"Simple and Efficient Construction of Static Single Assignment Form"
 * by Matthias Braun, Sebastian Buchwald, Sebastian Hack, Roland Leissa,
 *    Christoph Mallon and Andreas Zwinkau.
 * cfr. http://www.cdl.uni-saarland.de/papers/bbhlmz13cc.pdf
 */

#include <dmr_c.h>
#include <ptrmap.h>
#include <symbol.h>
#include <linearize.h>
#include <ssa.h>
#include <flow.h>
#include <assert.h>

DECLARE_PTRMAP(phi_map, struct basic_block *, pseudo_t);

static pseudo_t load_var_(struct dmr_C *C, struct basic_block*, struct symbol*, unsigned long);


static inline void concat_user_list(struct pseudo_user_list *src, struct pseudo_user_list **dst)
{
	ptrlist_concat((struct ptr_list *)src, (struct ptr_list **)dst);
}

/*
 * Go through the "insn->users" list and replace them all..
 */
static void convert_phi(struct dmr_C *C, struct instruction *insn, pseudo_t src)
{
	pseudo_t phi = insn->target;
	struct pseudo_user *pu;

	assert(phi != src);
	if (phi == src)
		return;
	FOR_EACH_PTR(phi->users, pu) {
		pseudo_t *pp = pu->userp;
		pseudo_t p = *pp;

		if (p == VOID_PSEUDO(C))
			continue;
		assert(p == phi);
		if (p->def->src == phi)
			continue;
		*pu->userp = src;
	} END_FOR_EACH_PTR(pu);
	if (dmrC_has_use_list(src))
		concat_user_list(phi->users, &src->users);
	phi->users = NULL;
}

static void kill_phi_node(struct dmr_C *C, struct instruction *node)
{
	struct pseudo_list *phi_list = node->phi_list;
	pseudo_t phi;

	node->phi_list = NULL;
	node->bb = NULL;
	FOR_EACH_PTR(phi_list, phi) {
		struct instruction *phisrc;
		if (phi == VOID_PSEUDO(C))
			continue;
		phisrc = phi->def;
		dmrC_remove_use(C, &phisrc->src);
		phisrc->bb = NULL;
	} END_FOR_EACH_PTR(phi);
	ptrlist_remove_all((struct ptr_list **)&node->phi_list);
}

static pseudo_t try_remove_trivial_phi(struct dmr_C *C, pseudo_t phi)
{
	struct instruction *node = phi->def;
	pseudo_t same = NULL;
	pseudo_t p;

	if (phi == VOID_PSEUDO(C))
		return phi;
	if (phi->type != PSEUDO_REG)
		return phi;
	if (node->opcode != OP_PHI)
		return phi;
	FOR_EACH_PTR(node->phi_list, p) {
		pseudo_t src;

		if (p == VOID_PSEUDO(C))
			continue;
		src = p->def->src;
		if (src == same || src == phi)
			continue;
		if (same)
			return phi;
		same = src;
	} END_FOR_EACH_PTR(p);

	if (same == NULL)
		same = dmrC_undef_pseudo(C);

	convert_phi(C, node, same);
	phi->target = same;
	phi->type = PSEUDO_INDIR;
	kill_phi_node(C, node);

	return same;
}

// FIXME: -> common file
static void append_instruction(struct dmr_C *C, struct basic_block *bb, struct instruction *insn)
{
	struct instruction *br = dmrC_delete_last_instruction(&bb->insns);
	insn->bb = bb;
	dmrC_add_instruction(C, &bb->insns, insn);
	dmrC_add_instruction(C, &bb->insns, br);
}

static pseudo_t add_phi_operand(struct dmr_C *C, struct symbol *var, pseudo_t phi, unsigned long generation)
{
	struct instruction *node = phi->def;
	struct basic_block *parent;

	assert(node->opcode == OP_PHI);
	FOR_EACH_PTR(node->bb->parents, parent) {
		pseudo_t val = load_var_(C, parent, var, generation);
		struct instruction *phisrc = dmrC_alloc_phisrc(C, val, var);
		pseudo_t src = phisrc->target;
		append_instruction(C, parent, phisrc);
		dmrC_use_pseudo(C, node, src, dmrC_add_pseudo(C, &node->phi_list, src));
		src->ident = var->ident;
	} END_FOR_EACH_PTR(parent);
	phi = try_remove_trivial_phi(C, phi);
	return phi;
}

static pseudo_t load_var_(struct dmr_C *C, struct basic_block *bb, struct symbol *var, unsigned long generation)
{
	pseudo_t val = phi_map_lookup(var->phi_map, bb);
	unsigned long bbgen = bb->generation;

	if (val) {
		while (val->type == PSEUDO_INDIR)
			val = val->target;
		return val;
	}

	if (!bb->sealed) {	// incomplete CFG
		val = dmrC_insert_phi_node(C, bb, var);
		val->def->var = var;
		val->ident = var->ident;
		goto out;
	}

	switch (dmrC_bb_list_size(bb->parents)) {
	case 0:	// never defined
		val = dmrC_undef_pseudo(C);
		break;
	case 1:
		if (bbgen == generation)
			goto cycle;
		bb->generation = generation;
		// no phi needed
		val = load_var_(C, dmrC_first_basic_block(bb->parents), var, generation);
		break;
	default:
	cycle:
		val = dmrC_insert_phi_node(C, bb, var);
		val->ident = var->ident;
		dmrC_store_var(C, bb, var, val);
		val = add_phi_operand(C, var, val, generation);
	}

out:
	assert(val->type != PSEUDO_INDIR);
	dmrC_store_var(C, bb, var, val);
	return val;
}

pseudo_t dmrC_load_var(struct dmr_C *C, struct basic_block *bb, struct symbol *var)
{
	return load_var_(C, bb, var, ++C->L->bb_generation);
}

void dmrC_store_var(struct dmr_C *C, struct basic_block *bb, struct symbol *var, pseudo_t val)
{
	phi_map_update(&var->phi_map, bb, val, &C->ptrmap_allocator);
}

void dmrC_seal_bb(struct dmr_C *C, struct basic_block *bb)
{
	struct instruction *insn;
	if (bb->sealed || bb->unsealable)
		return;
	FOR_EACH_PTR(bb->insns, insn) {
		struct symbol *var;
		if (!insn->bb)
			continue;
		if (insn->opcode != OP_PHI)
			break;
		var = insn->var;
		assert(var);
		insn->var = NULL;
		add_phi_operand(C, var, insn->target, ++C->L->bb_generation);
	} END_FOR_EACH_PTR(insn);
	bb->sealed = 1;
}

void dmrC_seal_gotos(struct dmr_C *C, struct entrypoint *ep)
{
	struct basic_block *bb;

	FOR_EACH_PTR(ep->bbs, bb) {
		if (bb->sealed)
			continue;
		if (bb->unsealable)
			bb->unsealable = 0;
		dmrC_seal_bb(C, bb);
	} END_FOR_EACH_PTR(bb);

	// cleanup these fields as they are aliased to ::needs & ::defines
	FOR_EACH_PTR(ep->bbs, bb) {
		bb->sealed = bb->unsealable = 0;
	} END_FOR_EACH_PTR(bb);
}
