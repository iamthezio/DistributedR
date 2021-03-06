/*
* Copyright [2014] Hewlett-Packard Development Company, L.P.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/
#include"hpdRF.hpp"
#include<time.h>
#include<string.h>

/* calculates how many bytes to allocate for buffer to serialize tree
   @param tree - tree to be serialized
 */

//8 bytes for prediction, 12 for treeID, split_variable, split_criteria_length
//however many bytes for split_criteria + 4 bytes for indicating if left,right,additional_info are null, 16 possible bytes for additional_info and 12*num_obs bytes for indices, weights
long long calculateBufferSize(hpdRFnode* tree)
{
  long long total = sizeof(double) + 4*sizeof(int);
  total += tree->split_criteria_length*sizeof(double);
  if(tree->additional_info)
    total += 5*sizeof(int)+
      tree->additional_info->num_obs*(sizeof(double) + sizeof(int));
  if(tree->summary_info)
    total += 2*sizeof(int) + 
      sizeof(double)*(3 + tree->summary_info->node_counts_length);

  if(tree->left)
    total += calculateBufferSize(tree->left);
  if(tree->right)
    total += calculateBufferSize(tree->right);
  return total;
}

/* serializes the tree in custom format
   @param tree - tree to be serialized
   @param buffer - buffer to be written into
 */
int* serializeTree(hpdRFnode *tree, int* buffer)
{
  double* temp = (double*) buffer;
  *(temp++) = tree->prediction;

  buffer = (int *) temp;
  *(buffer++) = tree->treeID;
  *(buffer++) = tree->split_variable;
  *(buffer++) = tree->split_criteria_length;

  temp = (double *)buffer;
  for(int i = 0; i < tree->split_criteria_length; i++)
    temp[i] = tree->split_criteria[i];

  temp += tree->split_criteria_length;
  buffer = (int *) temp;

  *buffer = 0;
  *buffer += (tree->additional_info != NULL) << 1;
  *buffer += (tree->summary_info != NULL) << 2;
  *buffer += (tree->left != NULL) << 3;
  *buffer += (tree->right != NULL) << 4;
  buffer++;

  if(tree->additional_info)
    {
      *(buffer++) = tree->additional_info->attempted;
      *(buffer++) = tree->additional_info->completed;
      *(buffer++) = tree->additional_info->leafID;
      *(buffer++) = tree->additional_info->num_obs;
      *(buffer++) = tree->additional_info->depth;
      temp = (double *) buffer;
      for(int i = 0; i < tree->additional_info->num_obs; i++)
	temp[i] = tree->additional_info->weights[i];
      temp += tree->additional_info->num_obs;
      buffer = (int *) temp;
      for(int i = 0; i < tree->additional_info->num_obs; i++)
	buffer[i] = tree->additional_info->indices[i];
      buffer += tree->additional_info->num_obs;
    }
  if(tree->summary_info)
    {
      *(buffer++) = tree->summary_info->n;
      *(buffer++) = tree->summary_info->node_counts_length;
      temp = (double *) buffer;
      *(temp++) = tree->summary_info->wt;
      *(temp++) = tree->summary_info->complexity;
      *(temp++) = tree->summary_info->deviance;
      for(int i = 0; i < tree->summary_info->node_counts_length; i++)
	*(temp++) = tree->summary_info->node_counts[i];
      buffer = (int *) temp;
    }

  if(tree->left)
    buffer = serializeTree(tree->left, buffer);
  if(tree->right)
    buffer = serializeTree(tree->right, buffer);
  return buffer;
}

/* function to unserialize the tree=
   @param tree - tree to be written into
   @param buffer - buffer to be read from 
   @param leaf_nodes - array of leaf nodes to be allocated 
 */
int* unserializeTree(hpdRFnode *tree, int* buffer, hpdRFnode**leaf_nodes)
{
  double * temp = (double *)buffer;
  tree->prediction = *(temp++);

  buffer = (int *) temp;

  tree->treeID = *(buffer++);
  tree->split_variable = *(buffer++);
  tree->split_criteria_length = *(buffer++);

  if(tree->split_criteria_length > 0)
    {
      temp = (double *) buffer;
      tree->split_criteria = 
	(double *) malloc(sizeof(double)*(tree->split_criteria_length));
      
      for(int i = 0; i < tree->split_criteria_length; i++)
	tree->split_criteria[i] = temp[i];
      temp += tree->split_criteria_length;
      buffer = (int *) temp;
    }
  else
    {
      tree->split_criteria = NULL;
      tree->split_variable = 0;
    }
  int indicator = *(buffer++);
  if(indicator & (1 << 1))
    {
      tree->additional_info = (hpdRFNodeInfo *) malloc(sizeof(hpdRFNodeInfo));
      tree->additional_info->attempted = *(buffer++);
      tree->additional_info->completed = *(buffer++);
      tree->additional_info->leafID = *(buffer++);
      tree->additional_info->num_obs = *(buffer++);
      tree->additional_info->depth = *(buffer++);
      temp = (double *) buffer;
      tree->additional_info->weights = 
	(double *) malloc(sizeof(double)*tree->additional_info->num_obs);
      for(int i = 0; i < tree->additional_info->num_obs; i++)
	tree->additional_info->weights[i] = temp[i];
      temp += tree->additional_info->num_obs;
      buffer = (int *) temp;
      tree->additional_info->indices = 
	(int *) malloc(sizeof(int)*tree->additional_info->num_obs);
      for(int i = 0; i < tree->additional_info->num_obs; i++)
	tree->additional_info->indices[i] = buffer[i];
      buffer += tree->additional_info->num_obs;
      
      if(tree->additional_info->leafID > -1)
      	leaf_nodes[tree->additional_info->leafID-1] = tree;
    }
  else
    tree->additional_info = NULL;

  if(indicator & (1 << 2))
    {
      tree->summary_info = (hpdRFSummaryInfo *)malloc(sizeof(hpdRFSummaryInfo));
      tree->summary_info->n= *(buffer++);
      tree->summary_info->node_counts_length = *(buffer++);
      if(tree->summary_info->node_counts_length > 0)
	tree->summary_info->node_counts = (double *)
	  malloc(sizeof(double)*tree->summary_info->node_counts_length);
      else
	tree->summary_info->node_counts = NULL;
      double * temp = (double *)buffer;
      tree->summary_info->wt = *(temp++);
      tree->summary_info->complexity = *(temp++);
      tree->summary_info->deviance = *(temp++);
      for(int i = 0; i < tree->summary_info->node_counts_length; i++)
	tree->summary_info->node_counts[i] = *(temp++);
      buffer = (int *) temp;
    }
  else
    tree->summary_info = NULL;
  if(indicator & (1 << 3))
    {
      tree->left = (hpdRFnode *)malloc(sizeof(hpdRFnode));
      buffer = unserializeTree(tree->left, buffer, leaf_nodes);
      if(tree->left->additional_info)
	tree->left->additional_info->parent = tree;
    }
  else
    tree->left = NULL;
  if(indicator & (1 << 4))
    {
      tree->right = (hpdRFnode *)malloc(sizeof(hpdRFnode));
      buffer = unserializeTree(tree->right, buffer, leaf_nodes);
      if(tree->right->additional_info)
	tree->right->additional_info->parent = tree;
    }
  else
    tree->right = NULL;
  return buffer;
    
}

/* This function will print out the tree
   @param tree - tree to print out
   @param depth - the starting depth
   @param max_depth - the maximum depth to print
   @param classes - for classification trees to print out proper classes 
 */
SEXP printNode(hpdRFnode *tree, int depth, int max_depth, SEXP classes)
{
#define tab     for(int i = 0; i < depth; i++) printf("\t")
  
  if(depth > max_depth)
    {
      tab;
	printf("Ommitting subtree\n");
	return R_NilValue;
    }
  
    double prediction = tree->prediction;
    tab;
    int index = (int) prediction;
    if(classes != R_NilValue && TYPEOF(classes) == STRSXP && 
       index >= 0 && index < length(classes))
      printf("<prediction> %s </prediction>\n",
	     CHAR(STRING_ELT(classes,(int)prediction)));
    else
      printf("<prediction> %f </prediction>\n", prediction);
    

    
    double* split_criteria = tree->split_criteria;
    int split_var=tree->split_variable;
    if(split_criteria != NULL)
      {
	tab;
	printf("<split_criteria> ");
	for(int i = 0; i < tree->split_criteria_length; i++)
	  printf("%f ",split_criteria[i]);
	printf("</split_criteria>\n");
	tab;
	printf("<split variable> %d </split variable>\n", split_var);
      }

    if(tree->additional_info)
      {

	tab;
	printf("leaf_id: %d\n", tree->additional_info->leafID);
	tab;
	printf("num_obs: %d\n", tree->additional_info->num_obs);

	/*
	tab;
	printf("indices: ");
	for(int i = 0; i < tree->additional_info->num_obs; i++)
	  printf("%d ", tree->additional_info->indices[i]);
	printf("\n");
	tab;
	printf("weights: ");
	for(int i = 0; i < tree->additional_info->num_obs; i++)
	  printf("%f ", tree->additional_info->weights[i]);
	printf("\n");
	*/
      }
    if(tree->summary_info)
      {
	tab;
	printf("n: %d\n",tree->summary_info->n);
	tab;
	printf("wt: %f\n",tree->summary_info->wt);
	tab;
	printf("complexity: %f\n",tree->summary_info->complexity);
	tab;
	printf("deviance: %f\n",tree->summary_info->deviance);
	tab;
	printf("node_counts: ");
	for(int i = 0; i < tree->summary_info->node_counts_length; i++)
	  printf("%f ",tree->summary_info->node_counts[i]);
	printf("\n");
      }
    if(tree->left != NULL)
      {
	tab;
	printf("<Left Child Node>\n");
	printNode(tree->left, 
		  depth+1,max_depth,classes);
	tab;
	printf("</Left Child Node>\n");
      }
    if(tree->right != NULL)
      {
	tab;
	printf("<Right Child Node>\n");
	printNode(tree->right, 
		  depth+1,max_depth,classes);
	tab;
	printf("</Right Child Node>\n");
      }
    
    return R_NilValue;
  }

/* If a node has 2 leafnodes that have same predictions, they can be collapsed
   @param tree - tree to be simplified
 */
void simplifyTree(hpdRFnode *tree)
{
  if(!tree->left && !tree->right)
    return;
  bool left=true,right=true;
  if(tree->left)
    {
      simplifyTree(tree->left);
      if(tree->left->left || tree->left->right)
	left = false;
    }
  if(tree->right)
    {
      simplifyTree(tree->right);
      if(tree->right->left || tree->right->right)
	right = false;
    }
  if(left && right &&
     tree->left->prediction == tree->right->prediction)
    {
      if(tree->left)
	destroyTree(tree->left);
      if(tree->right)
	destroyTree(tree->right);
      tree->left = NULL;
      tree->right = NULL;
      free(tree->split_criteria);
      tree->split_criteria_length = 0;
      tree->split_criteria = NULL;
      tree->split_variable = -1;
    }

}

/* garbage collect the tree
   @param tree - tree that will be garbage collected
 */
void destroyTree(hpdRFnode *tree)
{
  
  if(tree->additional_info != NULL)
    {
      free(tree->additional_info->weights);
      free(tree->additional_info->indices);
      free(tree->additional_info);
      tree->additional_info = NULL;
    }

  if(tree->summary_info != NULL)
    {
      if(tree->summary_info->node_counts_length > 0)
	free(tree->summary_info->node_counts);
      free(tree->summary_info);
      tree->summary_info = NULL;
    }

  if(tree->split_criteria != NULL)
    free(tree->split_criteria);
  if(tree->left != NULL)
    destroyTree(tree->left);
  if(tree->right != NULL)
    destroyTree(tree->right);
  free(tree);
}



/*
 This function creates a new child node
 @param parent - parent node
 @param response_categorical - number of categories for response
 @param child_node_observations - observations to initialize child node with
 @param child_weights - weights to initialize child node with
 @param nrow - number of rows
 @param features_num - number of features to choose for new node
 */
hpdRFnode* createChildNode(hpdRFnode* parent,  
			   bool copy, int* child_node_observations, 
			   double* child_weights, int child_num_obs,
			   int features_num)
{
  hpdRFnode* child = (hpdRFnode*) malloc(sizeof(hpdRFnode));
  child->additional_info = (hpdRFNodeInfo *) malloc(sizeof(hpdRFNodeInfo));
  child->prediction = 0;
  child->split_variable = -1;
  child->additional_info->leafID = -1;
  child->additional_info->num_obs = child_num_obs;
  if(copy)
    {
      child->additional_info->indices = 
	(int *) malloc(sizeof(int)*child->additional_info->num_obs);
      memcpy(child->additional_info->indices, child_node_observations, 
	     sizeof(int)*child->additional_info->num_obs);
      child->additional_info->weights = 
	(double *) malloc(sizeof(double)*child->additional_info->num_obs);

      memcpy(child->additional_info->weights, child_weights,
	     sizeof(double)*child->additional_info->num_obs);      
    }
  else
    {
      child->additional_info->indices = child_node_observations;
      child->additional_info->weights = child_weights;
    }
  child->additional_info->completed = 0;
  child->additional_info->attempted = 0;

  if(parent && parent->summary_info)
    {
      child->summary_info=(hpdRFSummaryInfo *) malloc(sizeof(hpdRFSummaryInfo));
      child->summary_info->deviance = 0;
      child->summary_info->complexity = 1;
      child->summary_info->node_counts_length = 0;
      child->summary_info->node_counts = NULL;
    }
  else
    child->summary_info = NULL;
  child->left = NULL;
  child->right = NULL;
  child->split_criteria = NULL;
  child->split_criteria_length = 0;
  if(parent != NULL)
    {
      child->treeID = parent->treeID;
      child->additional_info->depth = parent->additional_info->depth+1;
      child->additional_info->parent = parent;
    }
  else
    {
      child->additional_info->depth = 1;
      child->additional_info->parent = NULL;
      child->treeID = -1;
    }
  return child;
}



/*
 remove all the unecessary data from the node
 @param node - node to remove unnecessary nodes
 @param features_min - array of minimum values of each feature
 @param features_max - array of maximum values of each feature
 @param features_cardinality - array of number of classes of each feature or NA if numerical
 @param bin_num - array of number of bins for each feature
 @return - nothing
 */
void cleanSingleNode(hpdRFnode *node)
{
  if(node->additional_info != NULL)
    {
      free(node->additional_info->indices);
      free(node->additional_info->weights);
      free(node->additional_info);
      node->additional_info = NULL;
    } 
}

/*
  this function counts how many nodes are in the tree up to a certain depth
  @param tree - tree to be counted
  @param remaining_depth - the depth to keep counoting 
 */
int countSubTree(hpdRFnode *tree, int remaining_depth)
{
  if(remaining_depth == 0)
    return 0;
  int total = 1;
  if(tree->left != NULL)
    total += countSubTree(tree->left, remaining_depth - 1);
  if(tree->right != NULL)
    total += countSubTree(tree->right, remaining_depth - 1);
  return total;
}

/*This function will reformat trees into randomForest package style trees
  @param tree - tree to be converted
  @param forest - output variable to be written to
  @param index - current location of where to write to in forest variable
  @param features_cardinality - cardinality of features or NA
  @param nrow - number of rows in forest variable
  @param treeID - which tree is being converted
 */
void convertTreeToRandomForest(hpdRFnode* tree, SEXP forest, int* index, 
		  int *features_cardinality, int nrow, int treeID)
{
  int *i = index;
  int id = *i;
  int* nodestatus = INTEGER(VECTOR_ELT(forest,0));
  int* bestvar = INTEGER(VECTOR_ELT(forest,1));
  int* treemap = INTEGER(VECTOR_ELT(forest,2));
  double* nodepred = REAL(VECTOR_ELT(forest,3));
  double* xbestsplit = REAL(VECTOR_ELT(forest,4));
  
  if(tree->split_criteria != NULL && tree->split_criteria_length > 0)
    {
      nodestatus[treeID*nrow + id] = 1;
      bestvar[treeID*nrow +id] = tree->split_variable;
      nodepred[treeID*nrow +id] = tree->prediction;

      if(features_cardinality[bestvar[treeID*nrow +id]-1] == NA_INTEGER)
	xbestsplit[treeID*nrow +id] = tree->split_criteria[0];
      else if(features_cardinality[bestvar[treeID*nrow +id]-1] < 31)
	{
	  register double split = 0;
	  for(int j = 0; j < tree->split_criteria_length; j++)
	    split += 1 << ((int)tree->split_criteria[j]-1);
	  xbestsplit[treeID*nrow +id] = split;
	}
      else
	printf("too many categorical variables can only support 31");
     
      (*i)++;
      treemap[treeID*2*nrow +id] = (*i)+1;
      convertTreeToRandomForest(tree->left,
		   forest, index, features_cardinality, nrow, treeID);
      (*i)++;
      treemap[treeID*2*nrow +id + nrow] = (*i)+1;
      convertTreeToRandomForest(tree->right,
		   forest, index, features_cardinality, nrow, treeID);
    }
  else
    {
      nodestatus[treeID*nrow +id] = -1;
      nodepred[treeID*nrow +id] = tree->prediction;
    }
}

/* this function converts trees to rpart format
   @param tree - tree to convert
   @param indices - output variable to write into
   @param n - output variable to write into 
   @param wt - output variable to write into
   @param var - output variable to write which  var was split on
   @param dev - deviance of node
   @param yval - output value of node
   @param complexity - complexity of node
   @param improve - output variable to write into
   @param split_index - what is index of split to use
   @param ncat - ncat variable from rpart
   @param rowID - rowID of tree
   @param parent_cp - cp of parent tree node
   @parm node_index - index of node in rpart variables
   @param features_cardinality - cardinality of features or NA
   @param csplit_count - count of categorical splits
   @param current_depth - depth of current node
   @param max_depth - maximum depth to allow 
*/
int convertTreetoRpart(hpdRFnode* tree, int* indices, int* n,
		       double *wt, int* var, double* dev, 
		       double* yval, double* complexity,
		       double* improve,
		       double* split_index, int* ncat, 
		       double* node_count, int offset, 
		       int rowID, double parent_cp, int node_index,
		       int* features_cardinality, int response_cardinality,
		       int* csplit_count,
		       int current_depth, int max_depth)
{
  indices[node_index] = rowID;
  n[node_index] = tree->summary_info->n;
  wt[node_index] = tree->summary_info->wt;
  var[node_index] = tree->split_criteria_length == 0? 0:tree->split_variable;
  dev[node_index] = tree->summary_info->deviance;
  yval[node_index] = tree->prediction;
  complexity[node_index] = parent_cp*tree->summary_info->complexity;
  improve[node_index] = (1-tree->summary_info->complexity)
    *tree->summary_info->deviance;
  if(response_cardinality != NA_INTEGER)
    for(int i = 0; i < response_cardinality; i ++)
      node_count[node_index + i*offset] = tree->summary_info->node_counts[i];


  if(tree->split_criteria_length == 0)
    {
      ncat[node_index] = NA_REAL;
      split_index[node_index] = NA_REAL;
    }
  else if(features_cardinality[tree->split_variable-1] == NA_INTEGER)
    {
      ncat[node_index] = -1;
      split_index[node_index] = tree->split_criteria[0];
    }
  else
    {
      ncat[node_index] = features_cardinality[tree->split_variable-1];
      split_index[node_index] = ++(*csplit_count);
    }

  parent_cp = complexity[node_index];

  if(tree->left && current_depth < max_depth)
    node_index = convertTreetoRpart(tree->left, indices, n,wt,var, dev,
				    yval, complexity, improve, split_index, 
				    ncat, node_count, offset, 
				    rowID*2, parent_cp, 
				    node_index+1, features_cardinality,
				    response_cardinality,
				    csplit_count,current_depth+1,
				    max_depth);
  if(tree->right && current_depth < max_depth)
    node_index = convertTreetoRpart(tree->right, indices, n,wt,var, dev,
				    yval, complexity, improve, split_index,
 				    ncat, node_count, offset, 
				    rowID*2+1, parent_cp, 
				    node_index+1, features_cardinality,
				    response_cardinality,
				    csplit_count,current_depth+1,
				    max_depth);
  return node_index;

}

/* function creates the csplit part of rpart model
   @param tree - tree to create from
   @param features_cardinality - cardinality of features or NA 
   @param csplit_count - count of categorical splits (index)
   @param int* csplit - buffer to write into 
   @param nrow - number of feature variables (nrow of csplit matrix)
 */

void populateCsplit(hpdRFnode* tree, int* features_cardinality, 
		    int* csplit_count, int* csplit, int nrow)
{
  if(tree->split_criteria_length != 0 && 
     features_cardinality[tree->split_variable-1] != NA_INTEGER)
    {
      for(int i = 0; i < tree->split_criteria_length; i++)
	csplit[*csplit_count + ((int) tree->split_criteria[i]-1)*nrow] = 2;
      (*csplit_count)++;
    }
  if(tree->left)
    populateCsplit(tree->left, features_cardinality, 
		   csplit_count, csplit, nrow);
  if(tree->right)
    populateCsplit(tree->right, features_cardinality, 
		   csplit_count, csplit, nrow);
}
