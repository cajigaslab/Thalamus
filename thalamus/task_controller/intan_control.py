class IntanController:
  def __init__(self, node):
    self.node = node
    super().__init__("ec_intan_rhs_interface")
    self.experiment = experiment # experiment defn struct
    self.imd = imd               # microdrive index

    _, self.i_stim_acqs = edf_tools.getAcqWithType(
        self.experiment, ['intan_rhs'])        
    self.i_stim_acqs = np.nonzero(self.i_stim_acqs)[0]

    self.done_subs = []
    self.multi_stimparam_pubs = []

    for i_stim_acq in range(len(self.i_stim_acqs)):
        acq = self.experiment.hardware.acquisition[self.i_stim_acqs[i_stim_acq]]
        ns = edf_tools.getAcqRosNamespace(acq)
        
        self.multi_stimparam_pubs.append( 
            self.create_publisher(Rhs2000MultiChannelStimParams, 
                ns + '/intan_multi_stim_param_cmd', 10))

        self.done_subs.append( 
            self.create_subscription(
                Empty,
                ns + '/intan_stim_param_done', self.done_sub_cb, 10))